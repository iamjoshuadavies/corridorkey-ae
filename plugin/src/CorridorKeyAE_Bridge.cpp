/*
    CorridorKeyAE_Bridge.cpp
    IPC bridge to the Python inference runtime.

    - Auto-launches the runtime subprocess on first connection attempt
    - Discovers the runtime port from subprocess stdout ("PORT:XXXX")
    - Reconnects with cooldown to avoid hammering on failure
    - Graceful shutdown on plugin unload
*/

#include "CorridorKeyAE_Bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <array>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    typedef int pid_t;                  // Windows: bridge only uses pid_t as a sentinel
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
    #define SETSOCKOPT_CAST(p) reinterpret_cast<const char*>(p)
    // usleep shim: microseconds -> std::this_thread sleep
    static inline void usleep(unsigned int usec) {
        std::this_thread::sleep_for(std::chrono::microseconds(usec));
    }
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <sys/wait.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <mach-o/dyld.h>
    #include <dlfcn.h>
    #include <limits.h>
    typedef int socket_t;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCKET close
    #define SETSOCKOPT_CAST(p) (p)
#endif

namespace corridorkey {

// =============================================================================
// Binary message helpers
// =============================================================================

static uint32_t ReadU32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
}

static void WriteU32BE(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8)  & 0xFF;
    p[3] = v & 0xFF;
}

// Maximum send()/recv() chunk size. Windows send/recv take `int`, so we
// loop in 1 MB chunks — large enough to be efficient, small enough to
// stay well under INT_MAX and avoid C4267 truncation warnings.
static constexpr size_t kSocketChunkMax = 1 * 1024 * 1024;

static bool SendAll(socket_t sock, const void* data, size_t len) {
    const char* p = reinterpret_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        size_t remaining = len - sent;
        int chunk = static_cast<int>(remaining > kSocketChunkMax ? kSocketChunkMax : remaining);
        auto n = send(sock, p + sent, chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool RecvAll(socket_t sock, void* data, size_t len) {
    char* p = reinterpret_cast<char*>(data);
    size_t received = 0;
    while (received < len) {
        size_t remaining = len - received;
        int chunk = static_cast<int>(remaining > kSocketChunkMax ? kSocketChunkMax : remaining);
        auto n = recv(sock, p + received, chunk, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

static bool SendMessage(socket_t sock, const std::vector<uint8_t>& payload) {
    uint8_t header[4];
    WriteU32BE(header, static_cast<uint32_t>(payload.size()));
    if (!SendAll(sock, header, 4)) return false;
    if (!payload.empty()) {
        if (!SendAll(sock, payload.data(), payload.size())) return false;
    }
    return true;
}

static bool RecvMessage(socket_t sock, std::vector<uint8_t>& payload) {
    uint8_t header[4];
    if (!RecvAll(sock, header, 4)) return false;
    uint32_t len = ReadU32BE(header);
    if (len > 256 * 1024 * 1024) return false;
    payload.resize(len);
    if (len > 0) {
        if (!RecvAll(sock, payload.data(), len)) return false;
    }
    return true;
}

// =============================================================================
// Runtime port handoff via temp file
// =============================================================================
// The runtime writes "<pid> <port>\n" to <temp>/corridorkey_runtime.port after
// it binds. Cross-platform; doesn't depend on stdout pipe inheritance through
// venv launchers (which is unreliable on Windows).

static std::string GetRuntimePortFilePath() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetTempPathA(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) return "";
    return std::string(buf) + "corridorkey_runtime.port";
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    return std::string(tmp) + "/corridorkey_runtime.port";
#endif
}

static int ReadRuntimePortFile() {
    std::string path = GetRuntimePortFilePath();
    if (path.empty()) return 0;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return 0;
    int pid = 0, port = 0;
    int matched = fscanf(f, "%d %d", &pid, &port);
    fclose(f);
    if (matched < 2 || port <= 0) return 0;
    return port;
}

static void DeleteRuntimePortFile() {
    std::string path = GetRuntimePortFilePath();
    if (path.empty()) return;
#ifdef _WIN32
    DeleteFileA(path.c_str());
#else
    unlink(path.c_str());
#endif
}

// =============================================================================
// Find repo/runtime paths from plugin binary location
// =============================================================================

#ifndef _WIN32
static std::string GetPluginBundlePath() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&GetPluginBundlePath), &info) && info.dli_fname) {
        // Resolve symlinks to get the real path (AE loads via symlink)
        char resolved[PATH_MAX];
        std::string path;
        if (realpath(info.dli_fname, resolved)) {
            path = resolved;
        } else {
            path = info.dli_fname;
        }
        // path = .../corridorkey-ae/build/plugin/CorridorKey.plugin/Contents/MacOS/CorridorKey
        // We want the repo root: strip /build/plugin/...
        auto pos = path.rfind("/build/plugin/");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
        // Fallback
        pos = path.rfind(".plugin/");
        if (pos != std::string::npos) {
            return path.substr(0, pos + 7);
        }
    }
    return "";
}
#else
// Windows: derive a base dir from the .aex location. Returns the directory
// containing the plugin DLL itself. On Windows the plugin is typically
// installed to Program Files (no relation to the repo), so the repo root
// must come from CORRIDORKEY_REPO_ROOT env var (handled in FindRepoRoot).
// This is kept for diagnostic/fallback purposes.
static std::string GetPluginBundlePath() {
    HMODULE hMod = NULL;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetPluginBundlePath),
            &hMod)) {
        return "";
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(hMod, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "";
    // Convert UTF-16 -> UTF-8 (best-effort, ASCII paths in practice)
    std::string path;
    path.reserve(n);
    int u8_len = WideCharToMultiByte(CP_UTF8, 0, buf, n, nullptr, 0, nullptr, nullptr);
    if (u8_len <= 0) return "";
    path.resize(u8_len);
    WideCharToMultiByte(CP_UTF8, 0, buf, n, path.data(), u8_len, nullptr, nullptr);
    // Strip filename, return parent directory using either separator
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos) path.resize(pos);
    return path;
}
#endif

// =============================================================================
// Private Implementation
// =============================================================================

using steady_clock = std::chrono::steady_clock;
using time_point = steady_clock::time_point;

struct RuntimeBridge::Impl {
    bool connected = false;
    socket_t sock = INVALID_SOCK;
    pid_t child_pid = -1;       // POSIX pid; on Windows used as a launched-flag (1 = launched)
    int runtime_port = 0;
    int stdout_pipe = -1;       // POSIX pipe fd (unused on Windows)

#ifdef _WIN32
    HANDLE win_proc = NULL;     // Child runtime process handle
    // Job object: every spawned runtime process (and any descendants,
    // like the venv launcher -> real python.exe chain) gets assigned to
    // this job. The job is created with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    // so when we close the handle — on Shutdown() or when the DLL unloads
    // — Windows guarantees the entire subtree is terminated. Without this,
    // TerminateProcess on just the launcher handle leaves the real python
    // orphaned and you accumulate abandoned runtimes over time.
    HANDLE win_job = NULL;
#endif

    // Thread safety: MFR calls render from multiple threads
    std::mutex bridge_mutex;

    // Reconnect cooldown: don't retry too fast
    time_point last_connect_attempt{};
    int connect_failures = 0;
    static constexpr int MAX_COOLDOWN_SEC = 10;

    // Repo root path (discovered from plugin binary)
    std::string repo_root;
    bool repo_root_resolved = false;

    void CloseSocket() {
        if (sock != INVALID_SOCK) {
            CLOSE_SOCKET(sock);
            sock = INVALID_SOCK;
        }
        connected = false;
    }

    bool ShouldAttemptConnect() {
        if (connect_failures == 0) return true;
        auto now = steady_clock::now();
        int cooldown_sec = std::min(connect_failures, MAX_COOLDOWN_SEC);
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_connect_attempt).count();
        return elapsed >= cooldown_sec;
    }

    void RecordConnectFailure() {
        last_connect_attempt = steady_clock::now();
        connect_failures++;
    }

    void RecordConnectSuccess() {
        connect_failures = 0;
    }

    // Path under a repo root that must exist for a candidate to be valid.
    // Used by FindRepoRoot() as the file-exists probe.
#ifdef _WIN32
    static constexpr const char* kRuntimePythonSubpath = "/runtime/.venv/Scripts/python.exe";
#else
    static constexpr const char* kRuntimePythonSubpath = "/runtime/.venv/bin/python3";
#endif

    static bool CandidateHasRuntime(const std::string& root) {
        if (root.empty()) return false;
        std::string probe = root + kRuntimePythonSubpath;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(probe.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
        return access(probe.c_str(), X_OK) == 0;
#endif
    }

    std::string FindRepoRoot() {
        if (repo_root_resolved) return repo_root;
        repo_root_resolved = true;

        std::vector<std::string> candidates;

        // 1. Dev escape hatch: CORRIDORKEY_REPO_ROOT env var. Still supported
        //    so running from a source checkout Just Works — but no longer
        //    required for packaged installs.
        if (const char* env = std::getenv("CORRIDORKEY_REPO_ROOT")) {
            if (env[0] != '\0') candidates.emplace_back(env);
        }

        // 2. Well-known install locations.
#ifdef _WIN32
        //    Per-user install: %LOCALAPPDATA%\CorridorKey
        if (const char* lad = std::getenv("LOCALAPPDATA")) {
            if (lad[0] != '\0') {
                candidates.emplace_back(std::string(lad) + "\\CorridorKey");
            }
        }
        //    System-wide install: %ProgramFiles%\CorridorKey
        if (const char* pf = std::getenv("ProgramFiles")) {
            if (pf[0] != '\0') {
                candidates.emplace_back(std::string(pf) + "\\CorridorKey");
            }
        }
#else
        //    Per-user install: ~/Library/Application Support/CorridorKey  (macOS)
        //                      ~/.local/share/CorridorKey                 (Linux)
        if (const char* home = std::getenv("HOME")) {
            if (home[0] != '\0') {
#  ifdef __MACH__
                candidates.emplace_back(
                    std::string(home) + "/Library/Application Support/CorridorKey");
#  else
                candidates.emplace_back(std::string(home) + "/.local/share/CorridorKey");
#  endif
            }
        }
#  ifdef __MACH__
        //    System-wide install: /Library/Application Support/CorridorKey
        //    This is where our macOS .pkg installer drops files — the .pkg
        //    runs as a localSystem install because AE's Plug-Ins/Effects
        //    folder under /Applications/... is only writable by root, so
        //    per-user installs can't deliver the plugin. System install
        //    handles both the runtime and the plugin copy in one go.
        candidates.emplace_back("/Library/Application Support/CorridorKey");
#  endif
#endif

        // 3. Plugin-bundle-relative fallback. On macOS dev workflow this
        //    resolves the AE symlink back into the build tree and finds the
        //    repo root. On Windows this is usually useless (the .aex lives
        //    in Program Files with no path-relationship to the runtime) but
        //    cheap to check.
        std::string bundle = GetPluginBundlePath();
        if (!bundle.empty()) candidates.emplace_back(bundle);

        // Return the first candidate where the runtime's Python is actually
        // present — that way we skip bogus paths silently instead of
        // trying to CreateProcess a missing file.
        for (const std::string& c : candidates) {
            if (CandidateHasRuntime(c)) {
                repo_root = c;
                return repo_root;
            }
        }

        // Nothing validated. Return empty so LaunchRuntime bails out cleanly.
        repo_root.clear();
        return repo_root;
    }

    void KillExistingRuntimes() {
#ifndef _WIN32
        // Kill any orphaned runtime processes from previous sessions
        // Uses pkill to find processes matching our server.main pattern
        system("pkill -f 'server.main --port' 2>/dev/null");
        usleep(100000); // 100ms for processes to die
#endif
    }

    bool LaunchRuntime() {
#ifndef _WIN32
        // Clean up any zombies from previous AE sessions
        KillExistingRuntimes();

        std::string root = FindRepoRoot();
        if (root.empty()) return false;

        std::string runtime_dir = root + "/runtime";

        // The venv python MUST be used (not resolved to system python)
        // because Python detects its venv from its own path, which makes
        // the venv's site-packages available automatically.
        std::string python = root + "/runtime/.venv/bin/python3";

        // Resolve symlink for the access check, but execute the venv path
        char resolved_python[PATH_MAX];
        bool venv_ok = false;
        if (realpath(python.c_str(), resolved_python)) {
            venv_ok = (access(resolved_python, X_OK) == 0);
        }

        if (!venv_ok) {
            // Venv not found — can't run without it (no corridorkey_mlx)
            return false;
        }

        // Create pipe for reading stdout (to get PORT:XXXX)
        int pipefd[2];
        if (pipe(pipefd) != 0) return false;

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return false;
        }

        if (pid == 0) {
            // Child process
            close(pipefd[0]); // Close read end
            dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
            close(pipefd[1]);

            // Change to runtime directory
            chdir(runtime_dir.c_str());

            // Exec the runtime server using the venv python
            // argv[0] must be the full path so Python detects its venv
            execl(python.c_str(), python.c_str(), "-m", "server.main",
                  "--port", "0", // Auto-assign port
                  nullptr);

            // If exec failed
            _exit(1);
        }

        // Parent process
        close(pipefd[1]); // Close write end
        child_pid = pid;
        stdout_pipe = pipefd[0];

        // Set pipe to non-blocking for reading
        int flags = fcntl(stdout_pipe, F_GETFL, 0);
        fcntl(stdout_pipe, F_SETFL, flags | O_NONBLOCK);

        // Wait up to 30 seconds for PORT:XXXX on stdout
        char buf[256] = {};
        int buf_pos = 0;
        auto start = steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(
                   steady_clock::now() - start).count() < 30) {
            struct pollfd pfd;
            pfd.fd = stdout_pipe;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 500); // 500ms poll
            if (ret > 0) {
                int n = read(stdout_pipe, buf + buf_pos, sizeof(buf) - buf_pos - 1);
                if (n > 0) {
                    buf_pos += n;
                    buf[buf_pos] = '\0';
                    // Look for PORT:XXXX
                    char* port_str = strstr(buf, "PORT:");
                    if (port_str) {
                        runtime_port = atoi(port_str + 5);
                        if (runtime_port > 0) {
                            return true;
                        }
                    }
                }
            }
            // Check if child exited
            int status;
            pid_t result = waitpid(child_pid, &status, WNOHANG);
            if (result == child_pid) {
                child_pid = -1;
                return false; // Child exited prematurely
            }
        }
        // Timeout
        return runtime_port > 0;
#else
        // ---------- Windows ----------
        std::string root = FindRepoRoot();
        if (root.empty()) return false;

        std::string runtime_dir = root + "/runtime";
        std::string python      = root + "/runtime/.venv/Scripts/python.exe";

        // Check the venv exists
        DWORD attr = GetFileAttributesA(python.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return false;
        }

        // Wipe any stale port file so we don't read it as our own.
        DeleteRuntimePortFile();

        // Build the command line. Quote the python path; argv0 must be the
        // venv python (not a copy elsewhere) so Python detects its venv.
        std::string cmdline = "\"" + python + "\" -m server.main --port 0";
        // CreateProcessW needs a writable wide buffer
        std::wstring wcmd;
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, nullptr, 0);
            wcmd.resize(n > 0 ? n : 0);
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, wcmd.data(), n);
        }
        std::wstring wcwd;
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, runtime_dir.c_str(), -1, nullptr, 0);
            wcwd.resize(n > 0 ? n : 0);
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, runtime_dir.c_str(), -1, wcwd.data(), n);
        }

        STARTUPINFOW si{};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        // Create a Job Object to contain the runtime + every process it
        // spawns (the venv python.exe launcher re-execs the real system
        // python, which would otherwise survive as an orphan when we
        // terminate the launcher). When win_job is closed — in Shutdown()
        // or automatically on DLL unload — Windows kills everything in it.
        if (win_job == NULL) {
            win_job = CreateJobObjectW(nullptr, nullptr);
            if (win_job != NULL) {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                info.BasicLimitInformation.LimitFlags =
                    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(
                    win_job, JobObjectExtendedLimitInformation,
                    &info, sizeof(info));
            }
        }

        PROCESS_INFORMATION pi{};
        // CREATE_SUSPENDED: don't let the child start executing until
        // after we've assigned it to the Job Object. Otherwise a child
        // that respawns fast could sneak out of the job tree.
        BOOL ok = CreateProcessW(
            nullptr,                // lpApplicationName
            wcmd.data(),            // lpCommandLine (writable)
            nullptr, nullptr,
            FALSE,                  // bInheritHandles — none
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr,                // lpEnvironment (inherit)
            wcwd.empty() ? nullptr : wcwd.c_str(),
            &si, &pi);

        if (!ok) return false;

        if (win_job != NULL) {
            AssignProcessToJobObject(win_job, pi.hProcess);
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        win_proc  = pi.hProcess;
        child_pid = 1; // launched-flag for the cross-platform code below

        // Wait up to 30s for the runtime to write its port to the temp file.
        // (More reliable than reading stdout through a venv launcher chain.)
        auto start = steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(
                   steady_clock::now() - start).count() < 30) {
            int port = ReadRuntimePortFile();
            if (port > 0) {
                runtime_port = port;
                return true;
            }
            // Check if the child died early
            DWORD exit_code = 0;
            if (GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
                return false;
            }
            Sleep(150);
        }
        return runtime_port > 0;
#endif
    }

    // Set send + recv timeout on a socket. Cross-platform — Windows takes
    // a DWORD of milliseconds, POSIX takes a struct timeval, and getting
    // it wrong silently makes timeouts behave nothing like you expect.
    static void SetSocketTimeoutMs(socket_t s, int ms) {
#ifdef _WIN32
        DWORD t = static_cast<DWORD>(ms);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
#else
        struct timeval tv;
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    bool ConnectToPort(int port) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) return false;

        // Short connect timeout
        SetSocketTimeoutMs(s, 500);

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        // htons takes u_short; cast explicitly so /W4 doesn't flag C4244.
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            CLOSE_SOCKET(s);
            return false;
        }

        // Connected — switch to longer I/O timeout for inference
        SetSocketTimeoutMs(s, 30000);

        sock = s;
        connected = true;
        return true;
    }
};

// =============================================================================
// Public API
// =============================================================================

RuntimeBridge::RuntimeBridge()
    : m_impl(std::make_unique<Impl>())
{
#ifdef _WIN32
    // Initialize WinSock once per bridge. Safe to call repeatedly; each
    // WSAStartup pairs with a WSACleanup in ~Impl via a static guard.
    static std::once_flag wsa_init_flag;
    std::call_once(wsa_init_flag, []() {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        // No matching WSACleanup: plugin lifetime == process lifetime in AE,
        // and calling cleanup on unload would race with other sockets.
    });
#endif
}

RuntimeBridge::~RuntimeBridge()
{
    Shutdown();
}

bool RuntimeBridge::EnsureConnected()
{
    std::lock_guard<std::mutex> lock(m_impl->bridge_mutex);

    // Already connected?
    if (m_impl->connected && m_impl->sock != INVALID_SOCK) {
        return true;
    }

    // Cooldown: don't retry too fast after failures
    if (!m_impl->ShouldAttemptConnect()) {
        return false;
    }

    // 1. If we have a known port, try to connect to it
    if (m_impl->runtime_port > 0) {
        if (m_impl->ConnectToPort(m_impl->runtime_port)) {
            m_impl->RecordConnectSuccess();
            return true;
        }
    }

    // 2. If no runtime running, try to launch one
    if (m_impl->child_pid <= 0) {
        if (m_impl->LaunchRuntime() && m_impl->runtime_port > 0) {
            // Give the server a moment to start accepting
            usleep(200000); // 200ms
            if (m_impl->ConnectToPort(m_impl->runtime_port)) {
                m_impl->RecordConnectSuccess();
                return true;
            }
        }
    }

    // 3. Fallback: try the dev port (12345) in case user started it manually
    if (m_impl->runtime_port != 12345) {
        if (m_impl->ConnectToPort(12345)) {
            m_impl->runtime_port = 12345;
            m_impl->RecordConnectSuccess();
            return true;
        }
    }

    m_impl->RecordConnectFailure();
    return false;
}

bool RuntimeBridge::ProcessFrame(const FrameRequest& request, FrameResponse& response)
{
    std::lock_guard<std::mutex> lock(m_impl->bridge_mutex);

    if (!m_impl->connected || m_impl->sock == INVALID_SOCK) {
        response.success = false;
        response.error_message = "Runtime not connected";
        return false;
    }

    // Build the frame message (extended format with params + optional alpha hint):
    // "FRAME" (5) + width(4) + height(4) + rowbytes(4)
    // + output_mode(1) + despill(4f) + despeckle(4f) + refiner(4f) + matte_cleanup(4f)
    // + quality_mode(1) + has_hint(1) + [hint dims] + pixel_data + [hint data]
    size_t header_size = 5 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 1 + 1; // 36 bytes base
    size_t hint_header_size = 0;
    if (request.has_alpha_hint) {
        hint_header_size = 4 + 4 + 4;
    }
    size_t total = header_size + hint_header_size +
                   request.pixel_data.size() + request.hint_pixel_data.size();
    std::vector<uint8_t> payload(total);

    memcpy(payload.data(), "FRAME", 5);
    WriteU32BE(payload.data() + 5, request.width);
    WriteU32BE(payload.data() + 9, request.height);
    WriteU32BE(payload.data() + 13, request.rowbytes);
    payload[17] = static_cast<uint8_t>(request.output_mode);

    auto writeFloat = [](uint8_t* p, float v) {
        uint32_t bits;
        memcpy(&bits, &v, 4);
        p[0] = (bits >> 24) & 0xFF;
        p[1] = (bits >> 16) & 0xFF;
        p[2] = (bits >> 8)  & 0xFF;
        p[3] = bits & 0xFF;
    };
    writeFloat(payload.data() + 18, request.despill);
    writeFloat(payload.data() + 22, request.despeckle);
    writeFloat(payload.data() + 26, request.refiner);
    writeFloat(payload.data() + 30, request.matte_cleanup);
    payload[34] = static_cast<uint8_t>(request.quality_mode);
    payload[35] = request.has_alpha_hint ? 1 : 0;

    size_t offset = 36;
    if (request.has_alpha_hint) {
        WriteU32BE(payload.data() + offset, request.hint_width);
        WriteU32BE(payload.data() + offset + 4, request.hint_height);
        WriteU32BE(payload.data() + offset + 8, request.hint_rowbytes);
        offset += 12;
    }
    if (!request.pixel_data.empty()) {
        memcpy(payload.data() + offset, request.pixel_data.data(), request.pixel_data.size());
        offset += request.pixel_data.size();
    }
    if (request.has_alpha_hint && !request.hint_pixel_data.empty()) {
        memcpy(payload.data() + offset, request.hint_pixel_data.data(), request.hint_pixel_data.size());
    }

    if (!SendMessage(m_impl->sock, payload)) {
        m_impl->CloseSocket();
        response.success = false;
        response.error_message = "Failed to send frame";
        return false;
    }

    // Receive response
    std::vector<uint8_t> resp_data;
    if (!RecvMessage(m_impl->sock, resp_data)) {
        m_impl->CloseSocket();
        response.success = false;
        response.error_message = "Failed to receive response";
        return false;
    }

    // Parse response: "FRAME" (5) + width(4) + height(4) + rowbytes(4) + pixel_data
    const size_t resp_header_size = 5 + 4 + 4 + 4; // 17 bytes
    if (resp_data.size() < resp_header_size || memcmp(resp_data.data(), "FRAME", 5) != 0) {
        // LOADING<detail> — engine is still loading (first-run download
        // + warmup). Not an error; tell the caller to render pass-through
        // with a "Loading model..." status line.
        if (resp_data.size() >= 7 && memcmp(resp_data.data(), "LOADING", 7) == 0) {
            response.success = false;
            response.loading = true;
            response.loading_detail = std::string(
                reinterpret_cast<char*>(resp_data.data()) + 7,
                resp_data.size() - 7
            );
            return false;
        }
        if (resp_data.size() > 5 && memcmp(resp_data.data(), "ERROR", 5) == 0) {
            response.success = false;
            response.error_message = std::string(
                reinterpret_cast<char*>(resp_data.data()) + 5,
                resp_data.size() - 5
            );
            return false;
        }
        response.success = false;
        response.error_message = "Invalid response format";
        return false;
    }

    response.width = ReadU32BE(resp_data.data() + 5);
    response.height = ReadU32BE(resp_data.data() + 9);
    response.rowbytes = ReadU32BE(resp_data.data() + 13);
    response.pixel_data.assign(
        resp_data.begin() + resp_header_size,
        resp_data.end()
    );
    response.success = true;
    return true;
}

bool RuntimeBridge::GetStatus(RuntimeStatus& status)
{
    std::lock_guard<std::mutex> lock(m_impl->bridge_mutex);

    if (!m_impl->connected || m_impl->sock == INVALID_SOCK) {
        status.model_state = "disconnected";
        return false;
    }

    std::string msg = "{\"type\":\"status\"}";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    if (!SendMessage(m_impl->sock, payload)) {
        m_impl->CloseSocket();
        return false;
    }

    std::vector<uint8_t> response;
    if (!RecvMessage(m_impl->sock, response)) {
        m_impl->CloseSocket();
        return false;
    }

    status.model_state = "connected";
    return true;
}

void RuntimeBridge::Shutdown()
{
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->bridge_mutex);

    if (m_impl->connected && m_impl->sock != INVALID_SOCK) {
        std::string msg = "{\"type\":\"shutdown\"}";
        std::vector<uint8_t> payload(msg.begin(), msg.end());
        SendMessage(m_impl->sock, payload);
    }
    m_impl->CloseSocket();

#ifdef _WIN32
    if (m_impl->win_proc) {
        // Give the runtime ~500 ms to exit cleanly after the JSON shutdown
        // we sent above. If it's still alive, we fall through to closing
        // the Job Object below, which kills the whole process tree
        // (launcher + real python) atomically via
        // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. Terminating only the
        // launcher handle would leave the real python orphaned, which is
        // how we were accumulating zombie runtimes.
        WaitForSingleObject(m_impl->win_proc, 500);
        CloseHandle(m_impl->win_proc);
        m_impl->win_proc = NULL;
    }
    if (m_impl->win_job) {
        CloseHandle(m_impl->win_job);   // triggers KILL_ON_JOB_CLOSE
        m_impl->win_job = NULL;
    }
    m_impl->child_pid = -1;
#else
    if (m_impl->stdout_pipe >= 0) {
        close(m_impl->stdout_pipe);
        m_impl->stdout_pipe = -1;
    }

    if (m_impl->child_pid > 0) {
        kill(m_impl->child_pid, SIGTERM);
        usleep(500000);
        int status;
        if (waitpid(m_impl->child_pid, &status, WNOHANG) == 0) {
            kill(m_impl->child_pid, SIGKILL);
            waitpid(m_impl->child_pid, &status, 0);
        }
        m_impl->child_pid = -1;
    }
#endif
}

bool RuntimeBridge::IsConnected() const
{
    return m_impl && m_impl->connected && m_impl->sock != INVALID_SOCK;
}

} // namespace corridorkey

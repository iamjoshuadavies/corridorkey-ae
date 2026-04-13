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

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
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

static bool SendAll(socket_t sock, const void* data, size_t len) {
    const char* p = reinterpret_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        auto n = send(sock, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static bool RecvAll(socket_t sock, void* data, size_t len) {
    char* p = reinterpret_cast<char*>(data);
    size_t received = 0;
    while (received < len) {
        auto n = recv(sock, p + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
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
#endif

// =============================================================================
// Private Implementation
// =============================================================================

using steady_clock = std::chrono::steady_clock;
using time_point = steady_clock::time_point;

struct RuntimeBridge::Impl {
    bool connected = false;
    socket_t sock = INVALID_SOCK;
    pid_t child_pid = -1;
    int runtime_port = 0;
    int stdout_pipe = -1;       // Pipe to read subprocess stdout

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

    std::string FindRepoRoot() {
        if (repo_root_resolved) return repo_root;
        repo_root_resolved = true;
#ifndef _WIN32
        repo_root = GetPluginBundlePath();
#endif
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
                  "--tile-size", "512",
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
        return false; // Windows: TODO
#endif
    }

    bool ConnectToPort(int port) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) return false;

        // Short connect timeout
        struct timeval connect_tv;
        connect_tv.tv_sec = 0;
        connect_tv.tv_usec = 500000; // 500ms
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &connect_tv, sizeof(connect_tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &connect_tv, sizeof(connect_tv));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            CLOSE_SOCKET(s);
            return false;
        }

        // Connected — set longer I/O timeout for inference
        struct timeval io_tv;
        io_tv.tv_sec = 30;
        io_tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &io_tv, sizeof(io_tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &io_tv, sizeof(io_tv));

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

    if (m_impl->stdout_pipe >= 0) {
        close(m_impl->stdout_pipe);
        m_impl->stdout_pipe = -1;
    }

    if (m_impl->child_pid > 0) {
#ifndef _WIN32
        kill(m_impl->child_pid, SIGTERM);
        usleep(500000);
        int status;
        if (waitpid(m_impl->child_pid, &status, WNOHANG) == 0) {
            kill(m_impl->child_pid, SIGKILL);
            waitpid(m_impl->child_pid, &status, 0);
        }
#endif
        m_impl->child_pid = -1;
    }
}

bool RuntimeBridge::IsConnected() const
{
    return m_impl && m_impl->connected && m_impl->sock != INVALID_SOCK;
}

} // namespace corridorkey

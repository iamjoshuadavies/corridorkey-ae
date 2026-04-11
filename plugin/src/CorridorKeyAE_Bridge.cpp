/*
    CorridorKeyAE_Bridge.cpp
    IPC bridge to the Python inference runtime.

    M2: TCP socket connection to the Python runtime process.
    Protocol: 4-byte big-endian length prefix + raw payload.
    Frame data: simple binary format (header + raw ARGB pixels).
*/

#include "CorridorKeyAE_Bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

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
    typedef int socket_t;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCKET close
#endif

namespace corridorkey {

// =============================================================================
// Simple binary message helpers (no msgpack dependency for M2)
//
// Message format:
//   [4 bytes] big-endian payload length
//   [N bytes] payload (JSON-ish text for control, raw bytes for frames)
//
// For frame data, we use a simple binary header:
//   "FRAME" (5 bytes) + width(4) + height(4) + rowbytes(4) + pixel_data
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

// Send a length-prefixed message
static bool SendMessage(socket_t sock, const std::vector<uint8_t>& payload) {
    uint8_t header[4];
    WriteU32BE(header, static_cast<uint32_t>(payload.size()));
    if (!SendAll(sock, header, 4)) return false;
    if (!payload.empty()) {
        if (!SendAll(sock, payload.data(), payload.size())) return false;
    }
    return true;
}

// Receive a length-prefixed message
static bool RecvMessage(socket_t sock, std::vector<uint8_t>& payload) {
    uint8_t header[4];
    if (!RecvAll(sock, header, 4)) return false;
    uint32_t len = ReadU32BE(header);
    if (len > 256 * 1024 * 1024) return false; // 256 MB max
    payload.resize(len);
    if (len > 0) {
        if (!RecvAll(sock, payload.data(), len)) return false;
    }
    return true;
}

// =============================================================================
// Private Implementation
// =============================================================================

struct RuntimeBridge::Impl {
    bool connected = false;
    socket_t sock = INVALID_SOCK;
    pid_t child_pid = -1;
    int runtime_port = 0;

    void CloseSocket() {
        if (sock != INVALID_SOCK) {
            CLOSE_SOCKET(sock);
            sock = INVALID_SOCK;
        }
        connected = false;
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
    if (m_impl->connected && m_impl->sock != INVALID_SOCK) {
        return true;
    }

    // Try to connect to an already-running runtime on the known port
    if (m_impl->runtime_port > 0) {
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCK) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_impl->runtime_port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            // Set longer timeout for inference I/O (10s)
            struct timeval io_tv;
            io_tv.tv_sec = 10;
            io_tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &io_tv, sizeof(io_tv));
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &io_tv, sizeof(io_tv));

            m_impl->sock = sock;
            m_impl->connected = true;

            // Send a ping to verify
            std::string ping = "{\"type\":\"ping\"}";
            std::vector<uint8_t> ping_data(ping.begin(), ping.end());
            if (SendMessage(sock, ping_data)) {
                std::vector<uint8_t> response;
                if (RecvMessage(sock, response)) {
                    // Got a response — we're connected
                    return true;
                }
            }
            // Failed to ping — close and retry
            m_impl->CloseSocket();
        } else {
            CLOSE_SOCKET(sock);
        }
    }

    // TODO: Auto-launch runtime subprocess
    // For M2, the runtime must be started manually:
    //   cd runtime && source .venv/bin/activate && python -m server.main --port 12345
    // The plugin tries to connect to port 12345 by default.
    if (m_impl->runtime_port == 0) {
        m_impl->runtime_port = 12345; // Default dev port
    }

    // Try connecting to the default port
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_impl->runtime_port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect timeout: short (200ms) so we don't block AE if runtime isn't up
    struct timeval connect_tv;
    connect_tv.tv_sec = 0;
    connect_tv.tv_usec = 200000; // 200ms
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &connect_tv, sizeof(connect_tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &connect_tv, sizeof(connect_tv));

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        // Once connected, set a longer timeout for inference (10s)
        struct timeval io_tv;
        io_tv.tv_sec = 10;
        io_tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &io_tv, sizeof(io_tv));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &io_tv, sizeof(io_tv));

        m_impl->sock = sock;
        m_impl->connected = true;
        return true;
    }

    CLOSE_SOCKET(sock);
    return false;
}

bool RuntimeBridge::ProcessFrame(const FrameRequest& request, FrameResponse& response)
{
    if (!IsConnected()) {
        response.success = false;
        response.error_message = "Runtime not connected";
        return false;
    }

    // Build the frame message (extended format with params + optional alpha hint):
    // "FRAME" (5) + width(4) + height(4) + rowbytes(4)
    // + output_mode(1) + despill(4f) + despeckle(4f) + refiner(4f) + matte_cleanup(4f)
    // + has_hint(1) + [hint_width(4) + hint_height(4) + hint_rowbytes(4)] (if has_hint)
    // + pixel_data + [hint_pixel_data] (if has_hint)
    size_t header_size = 5 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 1; // 39 bytes base
    size_t hint_header_size = 0;
    if (request.has_alpha_hint) {
        hint_header_size = 4 + 4 + 4; // hint dimensions
    }
    size_t total = header_size + hint_header_size +
                   request.pixel_data.size() + request.hint_pixel_data.size();
    std::vector<uint8_t> payload(total);

    // Magic
    memcpy(payload.data(), "FRAME", 5);
    // Dimensions
    WriteU32BE(payload.data() + 5, request.width);
    WriteU32BE(payload.data() + 9, request.height);
    WriteU32BE(payload.data() + 13, request.rowbytes);
    // Output mode
    payload[17] = static_cast<uint8_t>(request.output_mode);
    // Parameters as big-endian floats
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
    // Alpha hint flag
    payload[34] = request.has_alpha_hint ? 1 : 0;

    size_t offset = 35;
    if (request.has_alpha_hint) {
        WriteU32BE(payload.data() + offset, request.hint_width);
        WriteU32BE(payload.data() + offset + 4, request.hint_height);
        WriteU32BE(payload.data() + offset + 8, request.hint_rowbytes);
        offset += 12;
    }
    // Main pixel data
    if (!request.pixel_data.empty()) {
        memcpy(payload.data() + offset, request.pixel_data.data(), request.pixel_data.size());
        offset += request.pixel_data.size();
    }
    // Hint pixel data
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
    // Response always uses the 17-byte legacy header (no params in response)
    const size_t resp_header_size = 5 + 4 + 4 + 4; // 17 bytes
    if (resp_data.size() < resp_header_size || memcmp(resp_data.data(), "FRAME", 5) != 0) {
        // Check for error response
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
    if (!IsConnected()) {
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

    // For now, just mark as connected
    status.model_state = "connected";
    return true;
}

void RuntimeBridge::Shutdown()
{
    if (m_impl) {
        if (m_impl->connected && m_impl->sock != INVALID_SOCK) {
            // Send shutdown message
            std::string msg = "{\"type\":\"shutdown\"}";
            std::vector<uint8_t> payload(msg.begin(), msg.end());
            SendMessage(m_impl->sock, payload);
        }
        m_impl->CloseSocket();

        // Kill child process if we launched one
        if (m_impl->child_pid > 0) {
#ifndef _WIN32
            kill(m_impl->child_pid, SIGTERM);
            waitpid(m_impl->child_pid, nullptr, WNOHANG);
#endif
            m_impl->child_pid = -1;
        }
    }
}

bool RuntimeBridge::IsConnected() const
{
    return m_impl && m_impl->connected && m_impl->sock != INVALID_SOCK;
}

} // namespace corridorkey

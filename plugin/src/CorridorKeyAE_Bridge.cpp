/*
    CorridorKeyAE_Bridge.cpp
    IPC bridge to the Python inference runtime.

    M1: Stub implementation — no actual IPC yet.
    M2: Will implement local socket communication with msgpack framing.
*/

#include "CorridorKeyAE_Bridge.h"

namespace corridorkey {

// =============================================================================
// Private Implementation (pImpl)
// =============================================================================

struct RuntimeBridge::Impl {
    bool connected = false;
    // M2: socket handle, process handle, connection state, etc.
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
    // M2: Launch runtime process if needed, establish socket connection.
    // For now, just return false (not connected).
    return false;
}

bool RuntimeBridge::ProcessFrame(const FrameRequest& request, FrameResponse& response)
{
    if (!IsConnected()) {
        response.success = false;
        response.error_message = "Runtime not connected";
        return false;
    }

    // M2: Serialize request, send over socket, receive response.
    return false;
}

bool RuntimeBridge::GetStatus(RuntimeStatus& status)
{
    if (!IsConnected()) {
        status.model_state = "disconnected";
        return false;
    }

    // M2: Query runtime for status.
    return false;
}

void RuntimeBridge::Shutdown()
{
    if (m_impl && m_impl->connected) {
        // M2: Send shutdown command, close socket, terminate process.
        m_impl->connected = false;
    }
}

bool RuntimeBridge::IsConnected() const
{
    return m_impl && m_impl->connected;
}

} // namespace corridorkey

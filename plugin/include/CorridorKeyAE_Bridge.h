#pragma once

/*
    CorridorKeyAE_Bridge.h
    IPC bridge to the Python runtime process.

    Handles:
    - Runtime process lifecycle (launch, health check, shutdown)
    - Frame data serialization and transfer
    - Status and diagnostics retrieval
*/

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace corridorkey {

// Forward declarations
struct FrameRequest;
struct FrameResponse;
struct RuntimeStatus;

/**
 * Bridge to the Python inference runtime.
 * Manages the IPC connection and runtime process lifecycle.
 */
class RuntimeBridge {
public:
    RuntimeBridge();
    ~RuntimeBridge();

    // Non-copyable
    RuntimeBridge(const RuntimeBridge&) = delete;
    RuntimeBridge& operator=(const RuntimeBridge&) = delete;

    /**
     * Ensure the runtime process is running and connected.
     * Launches the process if not already running.
     * Returns true if the bridge is ready for requests.
     */
    bool EnsureConnected();

    /**
     * Send a frame for inference and wait for the result.
     * Returns false on failure (timeout, connection lost, etc).
     */
    bool ProcessFrame(const FrameRequest& request, FrameResponse& response);

    /**
     * Query the runtime for its current status (device, VRAM, model state).
     */
    bool GetStatus(RuntimeStatus& status);

    /**
     * Gracefully shut down the runtime process.
     */
    void Shutdown();

    /**
     * Check if the runtime is currently connected and responsive.
     */
    bool IsConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// --- Data structures for IPC ---

struct FrameRequest {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowbytes = 0;
    std::vector<uint8_t> pixel_data;    // ARGB 8bpc

    // Alpha hint layer (optional — from AE layer parameter)
    bool has_alpha_hint = false;
    uint32_t hint_width = 0;
    uint32_t hint_height = 0;
    uint32_t hint_rowbytes = 0;
    std::vector<uint8_t> hint_pixel_data;   // ARGB 8bpc

    // Parameters
    int32_t output_mode = 0;
    int32_t device_mode = 0;
    int32_t quality_mode = 0;
    bool low_memory = false;
    int32_t tile_size = 512;

    // Cleanup
    float despill = 0.5f;
    float despeckle = 0.0f;
    float refiner = 0.5f;
    float matte_cleanup = 0.0f;
    float brightness = 1.0f;
};

struct FrameResponse {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowbytes = 0;
    std::vector<uint8_t> pixel_data;    // ARGB 8bpc
    bool success = false;
    std::string error_message;
};

struct RuntimeStatus {
    std::string device_name;
    int64_t vram_total_mb = 0;
    int64_t vram_used_mb = 0;
    bool is_unified_memory = false;     // Apple Silicon
    std::string model_state;            // "ready", "downloading", "missing", "error"
    bool warmup_complete = false;
    std::string cache_state;
};

} // namespace corridorkey

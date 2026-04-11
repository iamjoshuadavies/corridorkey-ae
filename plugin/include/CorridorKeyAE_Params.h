#pragma once

/*
    CorridorKeyAE_Params.h
    Parameter IDs and setup for the effect control panel.
*/

#include "CorridorKeyAE.h"

namespace corridorkey {

// --- Parameter IDs (order matters for AE serialization) ---
enum ParamID : A_long {
    PARAM_INPUT = 0,    // Reserved: default input layer

    // Input group
    PARAM_INPUT_COLORSPACE,
    PARAM_ALPHA_HINT_ENABLE,
    PARAM_ALPHA_HINT_LAYER,

    // Alpha Generation group
    PARAM_ALPHA_MODE,           // Auto / External
    PARAM_ALPHA_MODEL,          // Model selection popup
    PARAM_PREPROCESS_ENABLE,

    // Inference group
    PARAM_DEVICE,               // Auto / CPU / GPU
    PARAM_LOW_MEMORY_MODE,
    PARAM_QUALITY_MODE,         // Quality / Performance
    PARAM_TILE_SIZE,

    // Cleanup group
    PARAM_DESPILL_STRENGTH,
    PARAM_DESPECKLE_STRENGTH,
    PARAM_REFINER_STRENGTH,
    PARAM_MATTE_CLEANUP,

    // Output group
    PARAM_OUTPUT_MODE,          // Processed / Matte / Foreground / Composite

    // Status (read-only display)
    PARAM_STATUS_DEVICE,
    PARAM_STATUS_VRAM,
    PARAM_STATUS_MODEL_STATE,
    PARAM_STATUS_WARMUP,

    PARAM_COUNT
};

// --- Output modes ---
enum class OutputMode : A_long {
    Processed = 0,
    Matte,
    Foreground,
    Composite,
};

// --- Device modes ---
enum class DeviceMode : A_long {
    Auto = 0,
    CPU,
    GPU,
};

// --- Quality modes ---
enum class QualityMode : A_long {
    Quality = 0,
    Performance,
};

// --- Alpha modes ---
enum class AlphaMode : A_long {
    Auto = 0,
    External,
};

/**
 * Register all effect parameters with After Effects.
 * Called during PF_Cmd_PARAMS_SETUP.
 */
A_Err SetupParams(PF_InData* in_data, PF_OutData* out_data);

} // namespace corridorkey

#pragma once

/*
    CorridorKeyAE_Params.h
    Parameter IDs and setup for the effect control panel.
*/

#include "CorridorKeyAE.h"

namespace corridorkey {

// --- Parameter IDs (order matters for AE serialization) ---
// Only include params that are actually registered in SetupParams.
// Add new IDs here AND register them in CorridorKeyAE_Params.cpp.
enum ParamID : A_long {
    PARAM_INPUT = 0,    // Reserved: default input layer

    // Registered params (M1)
    PARAM_OUTPUT_MODE,          // Processed / Matte / Foreground / Composite
    PARAM_DEVICE,               // Auto / CPU / GPU
    PARAM_QUALITY_MODE,         // Quality / Performance
    PARAM_LOW_MEMORY_MODE,
    PARAM_DESPILL_STRENGTH,
    PARAM_DESPECKLE_STRENGTH,
    PARAM_REFINER_STRENGTH,
    PARAM_MATTE_CLEANUP,

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

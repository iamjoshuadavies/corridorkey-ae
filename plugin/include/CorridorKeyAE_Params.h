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

    // Branding
    PARAM_ABOUT_BUTTON,         // "CorridorKey" label + "About" button

    // Registered params
    PARAM_OUTPUT_MODE,          // Processed / Matte / Foreground / Composite
    PARAM_ALPHA_HINT_LAYER,     // Layer: external alpha matte input
    PARAM_QUALITY_MODE,         // Resolution preset
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

// --- Quality modes (model tile/resolution) ---
// Order: fastest to highest quality. Values are 0-indexed after AE popup -1.
enum class QualityMode : A_long {
    Direct256 = 0,      // Downscale to 256, fastest
    Direct512,          // Downscale to 512, fast (~0.3s)
    Direct1024,         // Downscale to 1024 (slower)
    Tiled512,           // Full-res tiled (best quality, ~5s/frame 1080p)
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

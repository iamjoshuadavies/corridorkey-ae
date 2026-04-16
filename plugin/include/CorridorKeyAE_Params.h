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
//
// NOTE: param order changed in v0.2.0 (pre-1.0 breaking change).
// PARAM_HINT_MODE added, PARAM_OUTPUT_MODE moved to bottom.
enum ParamID : A_long {
    PARAM_INPUT = 0,    // Reserved: default input layer

    // Branding
    PARAM_ABOUT_BUTTON,         // "CorridorKey" label + "About" button

    // Registered params (top to bottom in the effect panel)
    PARAM_HINT_MODE,            // Auto Generate / Layer
    PARAM_ALPHA_HINT_LAYER,     // Layer: external alpha matte input (disabled when auto)
    PARAM_SCREEN_CLIP,          // Tighten auto-generated key (only relevant in Auto mode)
    PARAM_QUALITY_MODE,         // Resolution preset
    PARAM_DESPILL_STRENGTH,
    PARAM_DESPECKLE_STRENGTH,
    PARAM_REFINER_STRENGTH,
    PARAM_MATTE_CLEANUP,
    PARAM_OUTPUT_MODE,          // Alpha Hint / Matte / Foreground / Composite / Processed

    PARAM_COUNT
};

// --- Hint modes ---
enum class HintMode : A_long {
    AutoGenerate = 0,   // Built-in chroma keyer produces the alpha hint
    Layer,              // User supplies an external layer via PARAM_ALPHA_HINT_LAYER
};

// --- Output modes (ordered by pipeline stage) ---
enum class OutputMode : A_long {
    AlphaHint = 0,      // Show the hint buffer (auto or layer), skip runtime
    Matte,              // Model's output alpha channel
    Foreground,         // Extracted foreground RGB
    Composite,          // Foreground over black
    Processed,          // Final output with despill/despeckle/cleanup (DEFAULT)
};

// --- Quality modes (model tile/resolution) ---
// Order: fastest to highest quality. Values are 0-indexed after AE popup -1.
enum class QualityMode : A_long {
    Direct256 = 0,      // Downscale to 256, fastest
    Direct512,          // Downscale to 512, fast (~0.3s)
    Direct1024,         // Downscale to 1024 (slower)
    Tiled512,           // Full-res tiled (best quality, ~5s/frame 1080p)
};

/**
 * Register all effect parameters with After Effects.
 * Called during PF_Cmd_PARAMS_SETUP.
 */
A_Err SetupParams(PF_InData* in_data, PF_OutData* out_data);

/**
 * Map plugin OutputMode to the runtime's wire-format mode index.
 * Runtime expects: 0=processed, 1=matte, 2=foreground, 3=composite.
 * AlphaHint is never sent to the runtime (short-circuited in plugin).
 */
inline uint8_t RuntimeOutputMode(OutputMode mode) {
    switch (mode) {
        case OutputMode::Processed:  return 0;
        case OutputMode::Matte:      return 1;
        case OutputMode::Foreground: return 2;
        case OutputMode::Composite:  return 3;
        default:                     return 0;
    }
}

} // namespace corridorkey

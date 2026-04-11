/*
    CorridorKeyAE_Params.cpp
    Parameter registration for the effect control panel.
*/

#include "CorridorKeyAE_Params.h"

namespace corridorkey {

A_Err SetupParams(PF_InData* in_data, PF_OutData* out_data)
{
#if AE_SDK_AVAILABLE
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    // --- Output Mode ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Output Mode",
        4,                  // num_choices
        1,                  // default
        "Processed|Matte|Foreground|Composite",
        PARAM_OUTPUT_MODE
    );

    // --- Alpha Hint Layer ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER(
        "Alpha Hint",
        PF_LayerDefault_NONE,   // No default layer — user must select
        PARAM_ALPHA_HINT_LAYER
    );

    // --- Device ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Device",
        3,
        1,
        "Auto|CPU|GPU",
        PARAM_DEVICE
    );

    // --- Quality Mode ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Quality",
        2,
        1,
        "Quality|Performance",
        PARAM_QUALITY_MODE
    );

    // --- Low Memory Mode ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Low Memory Mode",
        "Enable",
        FALSE,
        0,
        PARAM_LOW_MEMORY_MODE
    );

    // --- Despill Strength ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Despill",
        0.0, 1.0,          // valid min/max
        0.0, 1.0,          // slider min/max
        0.5,                // default
        PF_Precision_HUNDREDTHS,
        0, 0,
        PARAM_DESPILL_STRENGTH
    );

    // --- Despeckle Strength ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Despeckle",
        0.0, 1.0,
        0.0, 1.0,
        0.0,
        PF_Precision_HUNDREDTHS,
        0, 0,
        PARAM_DESPECKLE_STRENGTH
    );

    // --- Refiner Strength ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Refiner",
        0.0, 1.0,
        0.0, 1.0,
        0.5,
        PF_Precision_HUNDREDTHS,
        0, 0,
        PARAM_REFINER_STRENGTH
    );

    // --- Matte Cleanup ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Matte Cleanup",
        0.0, 1.0,
        0.0, 1.0,
        0.0,
        PF_Precision_HUNDREDTHS,
        0, 0,
        PARAM_MATTE_CLEANUP
    );

    out_data->num_params = PARAM_COUNT;

    return err;
#else
    return PF_Err_NONE;
#endif
}

} // namespace corridorkey

/*
    CorridorKeyAE_Params.cpp
    Parameter registration for the effect control panel.
*/

#include "CorridorKeyAE_Params.h"
#include "CorridorKeyAE_UI.h"

namespace corridorkey {

A_Err SetupParams(PF_InData* in_data, PF_OutData* out_data)
{
#if AE_SDK_AVAILABLE
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    // --- Branding header (custom drawn control) ---
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_NO_DATA;
    PF_STRNNCPY(def.PF_DEF_NAME, "", sizeof(def.PF_DEF_NAME));
    def.ui_flags = PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL;
    def.ui_width = CK_UI_WIDTH;
    def.ui_height = CK_UI_HEIGHT;
    def.uu.id = PARAM_ABOUT_BUTTON;
    if ((err = PF_ADD_PARAM(in_data, -1, &def)) != PF_Err_NONE) return err;

    // Register for custom UI events
    RegisterCustomUI(in_data);

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
        4,                  // num_choices
        1,                  // default (Full Res Tiled)
        "Full Res (Tiled 512)|Fast (512)|Fastest (256)|High (1024)",
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

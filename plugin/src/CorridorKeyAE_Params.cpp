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

    // --- Hint Mode ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Hint Mode",
        2,                  // num_choices
        1,                  // default: Auto Generate (1-indexed)
        "Auto Generate|Layer",
        PARAM_HINT_MODE
    );

    // --- Alpha Hint Layer ---
    // Greyed out when Hint Mode == Auto Generate (handled in USER_CHANGED_PARAM)
    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER(
        "Alpha Hint",
        PF_LayerDefault_NONE,   // No default layer -- user must select
        PARAM_ALPHA_HINT_LAYER
    );

    // --- Screen Clip ---
    // Tightens the auto-generated key by crushing dark values to black.
    // Only meaningful when Hint Mode == Auto Generate. Higher = cleaner
    // background but may eat into soft edges.
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Screen Clip",
        0.0, 1.0,          // valid min/max
        0.0, 1.0,          // slider min/max
        0.75,               // default: aggressive threshold for clean keys
        PF_Precision_HUNDREDTHS,
        0, 0,
        PARAM_SCREEN_CLIP
    );

    // --- Quality Mode ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Quality",
        4,                  // num_choices
        4,                  // default: Full Res (Tiled) -- last item
        "Fastest (256)|Fast (512)|High (1024)|Full Res (Tiled)",
        PARAM_QUALITY_MODE
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

    // --- Output Mode (at the bottom -- inspect output after tweaking params) ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Output Mode",
        5,                  // num_choices
        5,                  // default: Processed (5th item, 1-indexed)
        "Alpha Hint|Matte|Foreground|Composite|Processed",
        PARAM_OUTPUT_MODE
    );

    out_data->num_params = PARAM_COUNT;

    return err;
#else
    return PF_Err_NONE;
#endif
}

} // namespace corridorkey

#pragma once

/*
    CorridorKeyAE_UI.h
    Custom UI drawing for the effect controls panel.
    Renders logo, branded text, and About link via Drawbot API.
*/

#include "CorridorKeyAE.h"

#if AE_SDK_AVAILABLE
#include "AE_EffectUI.h"
#endif

namespace corridorkey {

#if AE_SDK_AVAILABLE

/**
 * Handle PF_Cmd_EVENT for custom UI drawing.
 * Routes to DRAW, DO_CLICK, etc.
 */
A_Err HandleEvent(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output,
    PF_EventExtra*  event_extra
);

/**
 * Register the custom UI info during PARAMS_SETUP.
 * Call this after adding the custom control parameter.
 */
A_Err RegisterCustomUI(PF_InData* in_data);

// Custom control dimensions
constexpr int CK_UI_WIDTH  = 300;
constexpr int CK_UI_HEIGHT = 58;

#endif

} // namespace corridorkey

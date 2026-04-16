/*
    CorridorKeyAE.cpp
    Main plugin entry point — command dispatcher.

    Based on Adobe After Effects SDK Skeleton sample.
    Routes AE commands to the appropriate handler functions.
*/

#include "CorridorKeyAE.h"
#include "CorridorKeyAE_Params.h"
#include "CorridorKeyAE_Render.h"
#include "CorridorKeyAE_UI.h"

#if AE_SDK_AVAILABLE
#include "AE_EffectSuites.h"
#endif

#if AE_SDK_AVAILABLE

// =============================================================================
// Registration Entry Point — tells AE about this effect
// =============================================================================

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr    inPtr,
    PF_PluginDataCB2    inPluginDataCallBackPtr,
    SPBasicSuite*       inSPBasicSuitePtr,
    const char*         inHostName,
    const char*         inHostVersion)
{
    PF_Err result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        CK_PLUGIN_NAME,        // "CorridorKey"
        CK_PLUGIN_MATCH_NAME,  // "com.corridorkey.ae"
        CK_PLUGIN_CATEGORY,    // "Keying"
        AE_RESERVED_INFO,
        "EffectMain",           // Entry point function name
        "https://github.com/corridorkey-ae"
    );

    return result;
}

// =============================================================================
// Effect Entry Point — called per-command by AE
// =============================================================================

extern "C" DllExport PF_Err
EffectMain(
    PF_Cmd          cmd,
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output,
    void            *extra)
{
    PF_Err err = PF_Err_NONE;

    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = corridorkey::HandleAbout(in_data, out_data);
                break;

            case PF_Cmd_GLOBAL_SETUP:
                err = corridorkey::HandleGlobalSetup(in_data, out_data);
                break;

            case PF_Cmd_GLOBAL_SETDOWN:
                err = corridorkey::HandleGlobalSetdown(in_data, out_data);
                break;

            case PF_Cmd_PARAMS_SETUP:
                err = corridorkey::HandleParamsSetup(in_data, out_data, params);
                break;

            case PF_Cmd_SEQUENCE_SETUP:
                err = corridorkey::HandleSequenceSetup(in_data, out_data);
                break;

            case PF_Cmd_SEQUENCE_SETDOWN:
                err = corridorkey::HandleSequenceSetdown(in_data, out_data);
                break;

            case PF_Cmd_RENDER:
                err = corridorkey::HandleRender(in_data, out_data, params, output);
                break;

            case PF_Cmd_EVENT:
                err = corridorkey::HandleEvent(in_data, out_data, params, output,
                    reinterpret_cast<PF_EventExtra*>(extra));
                break;

            case PF_Cmd_UPDATE_PARAMS_UI:
                err = corridorkey::HandleUpdateParamsUI(in_data, out_data, params);
                break;

            case PF_Cmd_USER_CHANGED_PARAM:
                err = corridorkey::HandleUserChangedParam(in_data, out_data, params,
                    reinterpret_cast<PF_UserChangedParamExtra*>(extra));
                break;

            case PF_Cmd_SMART_PRE_RENDER:
                err = corridorkey::SmartPreRender(in_data, out_data,
                    reinterpret_cast<PF_PreRenderExtra*>(extra));
                break;

            case PF_Cmd_SMART_RENDER:
                err = corridorkey::SmartRender(in_data, out_data,
                    reinterpret_cast<PF_SmartRenderExtra*>(extra));
                break;

            default:
                break;
        }
    }
    catch (...) {
        // Never let exceptions escape to AE — it will crash the host.
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return err;
}

#endif // AE_SDK_AVAILABLE

// =============================================================================
// Command Handlers
// =============================================================================

namespace corridorkey {

A_Err HandleAbout(PF_InData* in_data, PF_OutData* out_data)
{
#if AE_SDK_AVAILABLE
    PF_SPRINTF(out_data->return_msg,
        "CorridorKey v%s\n\n"
        "Based on the green-screen keying technique created by\n"
        "Niko Pueringer of Corridor Digital (youtube.com/CorridorCrew).\n\n"
        "Physically accurate foreground/background unmixing\n"
        "powered by MLX on Apple Silicon and PyTorch/CUDA on Windows.",
        CK_VERSION_STRING
    );
#endif
    return PF_Err_NONE;
}

A_Err HandleGlobalSetup(PF_InData* in_data, PF_OutData* out_data)
{
#if AE_SDK_AVAILABLE
    out_data->my_version = PF_VERSION(
        CK_VERSION_MAJOR,
        CK_VERSION_MINOR,
        CK_VERSION_PATCH,
        PF_Stage_DEVELOP,
        0
    );

    out_data->out_flags =
        PF_OutFlag_PIX_INDEPENDENT |
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_CUSTOM_UI |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI;

    out_data->out_flags2 =
        PF_OutFlag2_SUPPORTS_SMART_RENDER |
        PF_OutFlag2_FLOAT_COLOR_AWARE |
        PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
#endif
    return PF_Err_NONE;
}

A_Err HandleGlobalSetdown(PF_InData* in_data, PF_OutData* out_data)
{
    // Cleanup: shut down bridge if running
    return PF_Err_NONE;
}

A_Err HandleParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    return SetupParams(in_data, out_data);
}

A_Err HandleSequenceSetup(PF_InData* in_data, PF_OutData* out_data)
{
    // Per-sequence initialization (allocate sequence data)
    return PF_Err_NONE;
}

A_Err HandleSequenceSetdown(PF_InData* in_data, PF_OutData* out_data)
{
    // Per-sequence teardown (free sequence data)
    return PF_Err_NONE;
}

A_Err HandleRender(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    return RenderEffect(in_data, out_data, params, output);
}

// Helper: show/hide Alpha Hint and Screen Clip rows based on Hint Mode.
// Auto Generate -> disable Alpha Hint layer, enable Screen Clip
// Layer         -> enable Alpha Hint layer, disable Screen Clip
//
// Must copy params before modifying — they're read-only in UPDATE_PARAMS_UI.
// Uses PF_ParamUtilsSuite3::PF_UpdateParamUI (pattern from SDK Supervisor sample).
static void UpdateHintModeVisibility(PF_InData* in_data, PF_ParamDef* params[])
{
#if AE_SDK_AVAILABLE
    auto hint_mode = static_cast<HintMode>(params[PARAM_HINT_MODE]->u.pd.value - 1);

    // Acquire PF_ParamUtilsSuite3
    const PF_ParamUtilsSuite3* param_suite = nullptr;
    PF_Err suite_err = in_data->pica_basicP->AcquireSuite(
        kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3,
        reinterpret_cast<const void**>(&param_suite));

    if (suite_err || !param_suite) return;

    // Alpha Hint layer: disabled when Auto Generate
    {
        PF_ParamDef copy;
        AEFX_CLR_STRUCT(copy);
        copy = *params[PARAM_ALPHA_HINT_LAYER];
        if (hint_mode == HintMode::AutoGenerate) {
            copy.ui_flags |= PF_PUI_DISABLED;
        } else {
            copy.ui_flags &= ~PF_PUI_DISABLED;
        }
        param_suite->PF_UpdateParamUI(in_data->effect_ref,
                                      PARAM_ALPHA_HINT_LAYER, &copy);
    }

    // Screen Clip: disabled when Layer mode
    {
        PF_ParamDef copy;
        AEFX_CLR_STRUCT(copy);
        copy = *params[PARAM_SCREEN_CLIP];
        if (hint_mode == HintMode::Layer) {
            copy.ui_flags |= PF_PUI_DISABLED;
        } else {
            copy.ui_flags &= ~PF_PUI_DISABLED;
        }
        param_suite->PF_UpdateParamUI(in_data->effect_ref,
                                      PARAM_SCREEN_CLIP, &copy);
    }

    in_data->pica_basicP->ReleaseSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3);
#endif
}

A_Err HandleUpdateParamsUI(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
#if AE_SDK_AVAILABLE
    UpdateHintModeVisibility(in_data, params);
#endif
    return PF_Err_NONE;
}

A_Err HandleUserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_UserChangedParamExtra* extra)
{
#if AE_SDK_AVAILABLE
    if (extra->param_index == PARAM_ABOUT_BUTTON) {
        PF_SPRINTF(out_data->return_msg,
            "CorridorKey v%s\n\n"
            "Based on the green-screen keying technique created by\n"
            "Niko Pueringer of Corridor Digital (youtube.com/CorridorCrew).\n\n"
            "Physically accurate foreground/background unmixing\n"
            "powered by MLX on Apple Silicon and PyTorch/CUDA on Windows.",
            CK_VERSION_STRING
        );
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }

    if (extra->param_index == PARAM_HINT_MODE) {
        UpdateHintModeVisibility(in_data, params);
    }
#endif
    return PF_Err_NONE;
}

} // namespace corridorkey

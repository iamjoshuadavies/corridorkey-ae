/*
    CorridorKeyAE.cpp
    Main plugin entry point — command dispatcher.

    Based on Adobe After Effects SDK Skeleton sample.
    Routes AE commands to the appropriate handler functions.
*/

#include "CorridorKeyAE.h"
#include "CorridorKeyAE_Params.h"
#include "CorridorKeyAE_Render.h"

#if AE_SDK_AVAILABLE

// =============================================================================
// Main Entry Point
// =============================================================================

extern "C" DllExport
PF_Err PluginMain(
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
        "%s v%s\n%s\n\nAdvanced green-screen keying for After Effects.",
        CK_PLUGIN_NAME,
        CK_VERSION_STRING,
        CK_PLUGIN_DESCRIPTION
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
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_PIX_INDEPENDENT;

    out_data->out_flags2 =
        PF_OutFlag2_SUPPORTS_SMART_RENDER |
        PF_OutFlag2_FLOAT_COLOR_AWARE;
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

} // namespace corridorkey

#pragma once

/*
    CorridorKeyAE.h
    Main plugin header — AE entry points and effect lifecycle.

    Based on the Adobe After Effects SDK Skeleton sample structure.
    When AE_SDK_AVAILABLE is 0, stubs are used for development without the SDK.
*/

#include "CorridorKeyAE_Version.h"

#if AE_SDK_AVAILABLE
    #include "AEConfig.h"
    #include "entry.h"
    #include "AE_Effect.h"
    #include "AE_EffectCB.h"
    #include "AE_EffectCBSuites.h"
    #include "AE_Macros.h"
    #include "Param_Utils.h"
    #include "String_Utils.h"
#else
    // Stub types for building without AE SDK (dev/CI)
    #include <cstdint>
    using A_long    = int32_t;
    using A_Err     = int32_t;
    using PF_Cmd    = int32_t;
    struct PF_InData;
    struct PF_OutData;
    struct PF_ParamDef;
    struct PF_LayerDef;

    #define PF_Err_NONE 0
    constexpr int PF_Cmd_ABOUT              = 0;
    constexpr int PF_Cmd_GLOBAL_SETUP       = 1;
    constexpr int PF_Cmd_GLOBAL_SETDOWN     = 2;
    constexpr int PF_Cmd_PARAMS_SETUP       = 3;
    constexpr int PF_Cmd_RENDER             = 4;
    constexpr int PF_Cmd_SEQUENCE_SETUP     = 5;
    constexpr int PF_Cmd_SEQUENCE_SETDOWN   = 6;
    constexpr int PF_Cmd_SEQUENCE_RESETUP   = 7;
#endif

// --- Effect lifecycle entry points ---

extern "C" {

#if AE_SDK_AVAILABLE
    /**
     * Registration entry point — tells AE about this effect.
     * Called once when the plugin is first scanned.
     */
    DllExport PF_Err PluginDataEntryFunction2(
        PF_PluginDataPtr    inPtr,
        PF_PluginDataCB2    inPluginDataCallBackPtr,
        SPBasicSuite*       inSPBasicSuitePtr,
        const char*         inHostName,
        const char*         inHostVersion
    );

    /**
     * Effect entry point — called per-command by After Effects.
     * Routes commands (about, setup, render, etc.) to handlers.
     */
    PF_Err EffectMain(
        PF_Cmd          cmd,
        PF_InData       *in_data,
        PF_OutData      *out_data,
        PF_ParamDef     *params[],
        PF_LayerDef     *output,
        void            *extra
    );
#endif

}

// --- Command handlers ---
namespace corridorkey {

    A_Err HandleAbout(PF_InData* in_data, PF_OutData* out_data);
    A_Err HandleGlobalSetup(PF_InData* in_data, PF_OutData* out_data);
    A_Err HandleGlobalSetdown(PF_InData* in_data, PF_OutData* out_data);
    A_Err HandleParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[]);
    A_Err HandleSequenceSetup(PF_InData* in_data, PF_OutData* out_data);
    A_Err HandleSequenceSetdown(PF_InData* in_data, PF_OutData* out_data);
    A_Err HandleRender(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);

} // namespace corridorkey

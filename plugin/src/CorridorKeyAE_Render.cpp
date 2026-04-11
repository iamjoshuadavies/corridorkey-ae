/*
    CorridorKeyAE_Render.cpp
    Frame rendering — M1 milestone is passthrough (copy input to output).
*/

#include "CorridorKeyAE_Render.h"
#include "CorridorKeyAE_Bridge.h"

namespace corridorkey {

A_Err RenderEffect(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output)
{
#if AE_SDK_AVAILABLE
    PF_Err err = PF_Err_NONE;

    // M1: Simple passthrough — copy input layer to output.
    // This proves the effect loads, receives frames, and writes output.
    // M2+ will route through the RuntimeBridge for actual inference.

    ERR(PF_COPY(&params[0]->u.ld, output, NULL, NULL));

    return err;
#else
    // Stub: no-op without SDK
    return PF_Err_NONE;
#endif
}

} // namespace corridorkey

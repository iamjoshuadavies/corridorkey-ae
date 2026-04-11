#pragma once

/*
    CorridorKeyAE_Render.h
    Frame rendering — extracts pixels, sends to runtime, writes output.
*/

#include "CorridorKeyAE.h"

namespace corridorkey {

/**
 * Main render handler. Extracts the input frame, sends it to the
 * runtime bridge for inference, and writes the result to the output layer.
 *
 * For M1 (shell milestone), this is a passthrough that copies input to output.
 */
A_Err RenderEffect(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output
);

} // namespace corridorkey

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
 * Fallback: stamps diagnostic text overlay when bridge is not connected.
 */
A_Err RenderEffect(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output
);

#if AE_SDK_AVAILABLE
/**
 * Draw a text string onto a PF_LayerDef using a built-in 5x7 bitmap font.
 * Text is rendered at (x, y) in pixel coordinates with the given color.
 * Scale multiplies the pixel size (1 = tiny, 3 = readable, 5+ = large).
 */
void DrawText(
    PF_LayerDef*    layer,
    int             x,
    int             y,
    const char*     text,
    PF_Pixel8       color,
    int             scale = 3
);

/**
 * Draw a filled rectangle onto a PF_LayerDef.
 */
void DrawRect(
    PF_LayerDef*    layer,
    int             x0, int y0,
    int             x1, int y1,
    PF_Pixel8       color
);
#endif

} // namespace corridorkey

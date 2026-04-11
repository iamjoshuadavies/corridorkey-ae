/*
    CorridorKeyAE_Render.cpp
    Frame rendering with diagnostic text overlay and bridge integration.
*/

#include "CorridorKeyAE_Render.h"
#include "CorridorKeyAE_Params.h"
#include "CorridorKeyAE_Bridge.h"

namespace corridorkey {

// =============================================================================
// Minimal 5x7 bitmap font (uppercase + digits + basic punctuation)
// Each character is 5 columns × 7 rows, stored as 7 bytes (1 bit per column).
// =============================================================================

#if AE_SDK_AVAILABLE

static const unsigned char FONT_5X7[][7] = {
    // space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (33)
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    // " (34)
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
    // # (35)
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
    // $ (36)
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    // % (37)
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    // & (38)
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D},
    // ' (39)
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
    // ( (40)
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    // ) (41)
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    // * (42)
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
    // + (43)
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    // , (44)
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    // - (45)
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    // . (46)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    // / (47)
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},
    // 0 (48)
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    // 1 (49)
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    // 2 (50)
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    // 3 (51)
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    // 4 (52)
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    // 5 (53)
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    // 6 (54)
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    // 7 (55)
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    // 8 (56)
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    // 9 (57)
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    // : (58)
    {0x00,0x00,0x04,0x00,0x04,0x00,0x00},
    // skip ; < = > ? (59-63) — fill with blanks
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    // @ (64)
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    // A-Z (65-90)
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

static int FontIndex(char c) {
    if (c >= 32 && c <= 90) return c - 32;
    // Lowercase → uppercase
    if (c >= 'a' && c <= 'z') return (c - 'a') + ('A' - 32);
    return 0; // space for unknown
}

// =============================================================================
// Pixel drawing helpers
// =============================================================================

static inline void SetPixel8(PF_LayerDef* layer, int x, int y, PF_Pixel8 color) {
    if (x < 0 || x >= layer->width || y < 0 || y >= layer->height) return;
    PF_Pixel8* row = reinterpret_cast<PF_Pixel8*>(
        reinterpret_cast<char*>(layer->data) + y * layer->rowbytes
    );
    // Alpha-blend: draw at full opacity
    row[x] = color;
}

void DrawRect(PF_LayerDef* layer, int x0, int y0, int x1, int y1, PF_Pixel8 color) {
    // Clamp
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > layer->width) x1 = layer->width;
    if (y1 > layer->height) y1 = layer->height;

    for (int y = y0; y < y1; y++) {
        PF_Pixel8* row = reinterpret_cast<PF_Pixel8*>(
            reinterpret_cast<char*>(layer->data) + y * layer->rowbytes
        );
        for (int x = x0; x < x1; x++) {
            // Semi-transparent blend
            A_u_char src_a = color.alpha;
            A_u_char inv_a = 255 - src_a;
            row[x].alpha = 255;
            row[x].red   = (color.red   * src_a + row[x].red   * inv_a) / 255;
            row[x].green = (color.green * src_a + row[x].green * inv_a) / 255;
            row[x].blue  = (color.blue  * src_a + row[x].blue  * inv_a) / 255;
        }
    }
}

void DrawText(PF_LayerDef* layer, int x, int y, const char* text, PF_Pixel8 color, int scale) {
    int cursor_x = x;
    for (const char* p = text; *p; p++) {
        int idx = FontIndex(*p);
        const unsigned char* glyph = FONT_5X7[idx];
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (0x10 >> col)) {
                    // Draw a scale×scale block for each pixel
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            SetPixel8(layer,
                                cursor_x + col * scale + sx,
                                y + row * scale + sy,
                                color);
                        }
                    }
                }
            }
        }
        cursor_x += 6 * scale; // 5 pixels + 1 gap, scaled
    }
}

#endif // AE_SDK_AVAILABLE

// =============================================================================
// Bridge singleton (lives across render calls)
// =============================================================================

static RuntimeBridge g_bridge;

// =============================================================================
// Main render
// =============================================================================

A_Err RenderEffect(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output)
{
#if AE_SDK_AVAILABLE
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE; // For ERR2 macro (checkin)

    // Copy input to output as baseline
    ERR(PF_COPY(&params[0]->u.ld, output, NULL, NULL));
    if (err) return err;

    // Try to connect to the Python runtime
    bool bridge_ok = g_bridge.EnsureConnected();

    if (bridge_ok) {
        // --- Bridge connected: send frame to Python for processing ---
        PF_LayerDef* input = &params[0]->u.ld;

        FrameRequest request;
        request.width = input->width;
        request.height = input->height;
        request.rowbytes = input->rowbytes;

        // Read effect parameters from AE
        request.output_mode = params[PARAM_OUTPUT_MODE]->u.pd.value - 1;  // AE popups are 1-indexed
        request.despill = params[PARAM_DESPILL_STRENGTH]->u.fs_d.value;
        request.despeckle = params[PARAM_DESPECKLE_STRENGTH]->u.fs_d.value;
        request.refiner = params[PARAM_REFINER_STRENGTH]->u.fs_d.value;
        request.matte_cleanup = params[PARAM_MATTE_CLEANUP]->u.fs_d.value;

        // Extract pixel data from the input layer
        size_t data_size = static_cast<size_t>(input->height) * input->rowbytes;
        request.pixel_data.resize(data_size);
        memcpy(request.pixel_data.data(), input->data, data_size);

        // Check out the alpha hint layer (if user selected one)
        PF_ParamDef hint_param;
        AEFX_CLR_STRUCT(hint_param);
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_ALPHA_HINT_LAYER,
                              in_data->current_time, in_data->time_step,
                              in_data->time_scale, &hint_param));
        if (!err && hint_param.u.ld.data) {
            PF_LayerDef* hint_layer = &hint_param.u.ld;
            request.has_alpha_hint = true;
            request.hint_width = hint_layer->width;
            request.hint_height = hint_layer->height;
            request.hint_rowbytes = hint_layer->rowbytes;
            size_t hint_size = static_cast<size_t>(hint_layer->height) * hint_layer->rowbytes;
            request.hint_pixel_data.resize(hint_size);
            memcpy(request.hint_pixel_data.data(), hint_layer->data, hint_size);
        }
        ERR2(PF_CHECKIN_PARAM(in_data, &hint_param));

        FrameResponse response;
        if (g_bridge.ProcessFrame(request, response) && response.success) {
            // Write processed pixels back to output
            size_t out_size = static_cast<size_t>(output->height) * output->rowbytes;
            if (response.pixel_data.size() >= out_size) {
                memcpy(output->data, response.pixel_data.data(), out_size);
            }
        } else {
            // Bridge error — show error overlay
            PF_Pixel8 bg = {180, 80, 0, 0};
            DrawRect(output, 0, 0, output->width, 50, bg);
            PF_Pixel8 white = {255, 255, 255, 255};
            DrawText(output, 10, 8, "CORRIDORKEY BRIDGE ERROR", white, 3);
        }
    } else {
        // --- Bridge offline: show fallback diagnostic overlay ---
        PF_Pixel8 bg = {180, 0, 0, 0}; // alpha=180, black
        int bar_height = 40;
        if (output->height > 200) bar_height = 60;
        DrawRect(output, 0, 0, output->width, bar_height, bg);

        PF_Pixel8 white = {255, 255, 255, 255};
        int text_scale = 3;
        if (output->height > 400) text_scale = 4;
        DrawText(output, 10, 8, "CORRIDORKEY M2", white, text_scale);

        PF_Pixel8 yellow = {255, 255, 200, 0};
        DrawText(output, 10, 8 + 7 * text_scale + 4, "BRIDGE: OFFLINE", yellow, 2);
    }

    return err;
#else
    return PF_Err_NONE;
#endif
}

} // namespace corridorkey

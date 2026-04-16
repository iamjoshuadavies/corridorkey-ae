#pragma once

/*
    CorridorKeyAE_AutoHint.h
    Built-in chroma keyer for auto-generating the alpha hint.

    Uses an HSV hue-rotation approach to detect the dominant screen
    color and generate a binary matte. Spill suppression is handled
    downstream by CorridorKey's own despill.
*/

#include <cstdint>
#include <vector>

namespace corridorkey {

// Key color in HSL, as detected by the auto-detect histogram.
struct KeyColorHSL {
    float hue;          // [0, 1]  (0.33 = green = 120 degrees)
    float saturation;   // [0, 1]
    float lightness;    // [0, 1]
};

/**
 * Auto-detect the dominant screen color from an ARGB 8bpc buffer.
 *
 * Builds a 256-bin hue histogram weighted by saturation and value,
 * ignoring desaturated / too-dark / too-bright pixels. Smooths with
 * box blur, finds the peak bin restricted to the green hue range
 * (72-170 degrees, i.e. hue 0.20-0.47).
 *
 * @param argb_pixels  ARGB 8bpc pixel data (byte order: A, R, G, B)
 * @param width        Image width in pixels
 * @param height       Image height in pixels
 * @param rowbytes     Bytes per row (may include padding)
 * @return             Detected key color in HSL
 */
KeyColorHSL AutoDetectKeyColor(const uint8_t* argb_pixels,
                               int width, int height, int rowbytes);

/**
 * Generate an alpha hint matte from an ARGB 8bpc input buffer.
 *
 * 1. Calls AutoDetectKeyColor to find the dominant screen color.
 * 2. Runs the chroma keyer per pixel to produce a raw matte.
 * 3. Applies a hard threshold (Screen Clip) to produce a binary
 *    black/white matte.
 * 4. Writes ARGB 8bpc output: A = 255, R = G = B = matte value
 *    (white = foreground, black = background).
 *
 * @param input_argb   Input ARGB 8bpc pixel data
 * @param width        Image width
 * @param height       Image height
 * @param rowbytes     Input row stride in bytes
 * @param output_argb  Pre-allocated output buffer (width * height * 4 bytes).
 *                     Written with ARGB where A=255, RGB=matte value.
 * @param clip_bg      Threshold (0-1). Raw matte values below this become
 *                     black (background), above become white (foreground).
 *                     Default 0.75.
 */
void GenerateAutoHint(const uint8_t* input_argb,
                      int width, int height, int rowbytes,
                      uint8_t* output_argb,
                      float clip_bg = 0.75f);

} // namespace corridorkey

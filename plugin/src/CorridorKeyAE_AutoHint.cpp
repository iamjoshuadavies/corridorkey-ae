/*
    CorridorKeyAE_AutoHint.cpp
    Built-in chroma keyer for auto-generating the alpha hint.

    Uses an HSV hue-rotation approach for chroma keying:
    - TransformHSV: single-matrix RGB hue/saturation/value rotation
    - GenerateMatteValue: per-pixel matte via hue-rotated color comparison
    - AutoDetectKeyColor: 256-bin hue histogram, weighted by sat/val
    - GenerateAutoHint: full pipeline (detect + matte + threshold)

    All processing is CPU, operates on ARGB 8bpc buffers.
*/

#include "CorridorKeyAE_AutoHint.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace corridorkey {

// ----------------------------------------------------------------
// Constants
// ----------------------------------------------------------------

static constexpr float kPi = 3.14159265358979323846f;

// Hue range for green screen detection (normalized 0-1).
// 0.20 = 72 degrees, 0.47 = 169 degrees.
// Covers the practical green range including yellow-green and cyan-green.
static constexpr float kGreenHueMin = 0.20f;
static constexpr float kGreenHueMax = 0.47f;

// Hardcoded keying defaults.
static constexpr float kDefaultSensitivity  = 1.0f;
static constexpr float kDefaultScreenBalance = 0.0f;
static constexpr float kDefaultClipBG       = 0.0f;
static constexpr float kDefaultClipFG       = 1.0f;

static constexpr int kHistogramBins = 256;

// ----------------------------------------------------------------
// RGB <-> HSL helpers (for auto-detect histogram)
// ----------------------------------------------------------------

struct HSL {
    float h, s, l;
};

static HSL RGBToHSL(float r, float g, float b)
{
    float cmax = std::max({r, g, b});
    float cmin = std::min({r, g, b});
    float delta = cmax - cmin;

    HSL hsl;
    hsl.l = (cmax + cmin) * 0.5f;

    if (delta <= 0.001f) {
        hsl.h = 0.0f;
        hsl.s = 0.0f;
    } else {
        // Saturation
        if (hsl.l < 0.5f)
            hsl.s = delta / (cmax + cmin);
        else
            hsl.s = delta / (2.0f - cmax - cmin);

        // Hue
        float dr = (((cmax - r) / 6.0f) + (delta / 2.0f)) / delta;
        float dg = (((cmax - g) / 6.0f) + (delta / 2.0f)) / delta;
        float db = (((cmax - b) / 6.0f) + (delta / 2.0f)) / delta;

        if      (r == cmax) hsl.h = db - dg;
        else if (g == cmax) hsl.h = (1.0f / 3.0f) + dr - db;
        else                hsl.h = (2.0f / 3.0f) + dg - dr;

        if (hsl.h < 0.0f)      hsl.h += 1.0f;
        else if (hsl.h > 1.0f) hsl.h -= 1.0f;
    }

    return hsl;
}

// HSL -> HSV (for histogram weighting by HSV saturation/value)
static void HSLToHSV(float s_hsl, float l, float& s_hsv, float& v)
{
    v = l + s_hsl * std::min(l, 1.0f - l);
    s_hsv = (v == 0.0f) ? 0.0f : 2.0f * (1.0f - l / v);
}

// ----------------------------------------------------------------
// Core keying math (HSV hue-rotation approach)
// ----------------------------------------------------------------

// Single-matrix RGB hue/saturation/value rotation.
// H = hue shift [0,1], S = saturation scale, V = value scale.
// Uses Rec.601 luma weights (0.299, 0.587, 0.114).
static void TransformHSV(float r, float g, float b,
                         float H, float S, float V,
                         float& out_r, float& out_g, float& out_b)
{
    float VSU = V * S * cosf(-H * 2.0f * kPi);
    float VSW = V * S * sinf(-H * 2.0f * kPi);

    out_r = (0.299f*V + 0.701f*VSU + 0.168f*VSW) * r
          + (0.587f*V - 0.587f*VSU + 0.330f*VSW) * g
          + (0.114f*V - 0.114f*VSU - 0.497f*VSW) * b;

    out_g = (0.299f*V - 0.299f*VSU - 0.328f*VSW) * r
          + (0.587f*V + 0.413f*VSU + 0.035f*VSW) * g
          + (0.114f*V - 0.114f*VSU + 0.292f*VSW) * b;

    out_b = (0.299f*V - 0.300f*VSU + 1.250f*VSW) * r
          + (0.587f*V - 0.588f*VSU - 1.050f*VSW) * g
          + (0.114f*V + 0.886f*VSU - 0.203f*VSW) * b;
}

// Per-pixel matte value.
// keyHue: detected key color hue [0,1]
// keySatOverSens: keyColor.saturation / sensitivity
// screenBalance: 0 = hard edges, 1 = soft edges
// Returns alpha [0,1]: 0 = transparent (keyed out), 1 = opaque (foreground).
static float GenerateMatteValue(float r, float g, float b,
                                float keyHue, float keySatOverSens,
                                float screenBalance)
{
    float sr, sg, sb;
    TransformHSV(r, g, b, -keyHue, 1.0f / keySatOverSens, 1.0f, sr, sg, sb);

    float rv = sg;  // non-key channel 1 after hue rotation
    float gv = sb;  // non-key channel 2 after hue rotation

    float comp = (1.0f - screenBalance) * std::max(rv, gv)
               + screenBalance * std::min(rv, gv);

    float alpha = 1.0f - (sr - comp);
    return std::max(0.0f, std::min(1.0f, alpha));
}

// ----------------------------------------------------------------
// Auto key color detection
// ----------------------------------------------------------------

KeyColorHSL AutoDetectKeyColor(const uint8_t* argb_pixels,
                               int width, int height, int rowbytes)
{
    // Build a 256-bin hue histogram weighted by saturation and value.
    // Hue histogram weighted by saturation and value.
    float hue_hist[kHistogramBins];
    std::memset(hue_hist, 0, sizeof(hue_hist));

    float total_sat = 0.0f;
    float sat_weight_sum = 0.0f;

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = argb_pixels + y * rowbytes;
        for (int x = 0; x < width; ++x) {
            // ARGB byte order
            // uint8_t a = row[x * 4 + 0];
            float r = row[x * 4 + 1] / 255.0f;
            float g = row[x * 4 + 2] / 255.0f;
            float b = row[x * 4 + 3] / 255.0f;

            HSL hsl = RGBToHSL(r, g, b);

            // Convert HSL -> HSV for weighting
            float hsv_s, hsv_v;
            HSLToHSV(hsl.s, hsl.l, hsv_s, hsv_v);

            // Weight: prefer saturated, mid-brightness pixels (the screen).
            // Weight: prefer saturated, mid-brightness pixels (the screen).
            float w_sat = std::max(0.0f, std::min(1.0f, (hsv_s - 0.2f) * 4.0f));
            w_sat *= w_sat;  // pow(x, 2)
            float w_bright = std::max(0.0f, std::min(1.0f, (0.9f - hsv_v) * 8.0f));
            float w_dark   = std::max(0.0f, std::min(1.0f, (hsv_v - 0.1f) * 8.0f));

            float weight = w_sat * w_bright * w_dark;
            if (weight <= 0.0001f) continue;

            int bin = std::max(0, std::min(kHistogramBins - 1,
                               static_cast<int>(hsl.h * (kHistogramBins - 1))));
            hue_hist[bin] += weight;

            // Accumulate weighted saturation for the detected key
            total_sat += hsl.s * weight;
            sat_weight_sum += weight;
        }
    }

    // Box-blur the histogram (3 passes, radius 1) to smooth noise.
    float temp[kHistogramBins];
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < kHistogramBins; ++i) {
            int lo = std::max(0, i - 1);
            int hi = std::min(kHistogramBins - 1, i + 1);
            temp[i] = (hue_hist[lo] + hue_hist[i] + hue_hist[hi]) / 3.0f;
        }
        std::memcpy(hue_hist, temp, sizeof(hue_hist));
    }

    // Find the peak bin within the green hue range.
    int green_min_bin = static_cast<int>(kGreenHueMin * (kHistogramBins - 1));
    int green_max_bin = static_cast<int>(kGreenHueMax * (kHistogramBins - 1));
    int peak_bin = green_min_bin;
    float peak_val = 0.0f;

    for (int i = green_min_bin; i <= green_max_bin; ++i) {
        if (hue_hist[i] > peak_val) {
            peak_val = hue_hist[i];
            peak_bin = i;
        }
    }

    KeyColorHSL result;
    result.hue = static_cast<float>(peak_bin) / static_cast<float>(kHistogramBins - 1);

    // Halve the detected saturation. The halving is critical: it produces
    // a lower keySatOverSens, which means a higher amplification factor
    // (1.0 / keySatOverSens) in TransformHSV, which creates a more
    // discriminating matte. Without the halving, green areas come back as
    // medium grey instead of clean black.
    float raw_sat = (sat_weight_sum > 0.0f)
                    ? (total_sat / sat_weight_sum)
                    : 0.7f;  // fallback for edge cases
    result.saturation = raw_sat * 0.5f;
    if (result.saturation < 0.1f) result.saturation = 0.1f;  // floor clamp

    result.lightness = 0.5f;     // mid-lightness (standard for keying)

    return result;
}

// ----------------------------------------------------------------
// Full auto-hint pipeline
// ----------------------------------------------------------------

void GenerateAutoHint(const uint8_t* input_argb,
                      int width, int height, int rowbytes,
                      uint8_t* output_argb,
                      float clip_bg)
{
    // 1. Detect the key color
    KeyColorHSL key = AutoDetectKeyColor(input_argb, width, height, rowbytes);

    // keySatOverSens = saturation / sensitivity
    float keySatOverSens = key.saturation / kDefaultSensitivity;
    if (keySatOverSens < 0.001f) keySatOverSens = 0.001f;  // avoid div-by-zero

    // 2. Per-pixel matte generation
    int out_rowbytes = width * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t* in_row  = input_argb + y * rowbytes;
        uint8_t*       out_row = output_argb + y * out_rowbytes;

        for (int x = 0; x < width; ++x) {
            float r = in_row[x * 4 + 1] / 255.0f;
            float g = in_row[x * 4 + 2] / 255.0f;
            float b = in_row[x * 4 + 3] / 255.0f;

            float alpha = GenerateMatteValue(r, g, b,
                                             key.hue, keySatOverSens,
                                             kDefaultScreenBalance);

            // Hard threshold: above clip_bg = white (255), below = black (0)
            uint8_t matte = (alpha >= clip_bg) ? 255 : 0;

            // ARGB output: fully opaque, grayscale matte in RGB
            // (white = foreground, black = background)
            out_row[x * 4 + 0] = 255;    // A -- always fully opaque
            out_row[x * 4 + 1] = matte;  // R
            out_row[x * 4 + 2] = matte;  // G
            out_row[x * 4 + 3] = matte;  // B
        }
    }
}

} // namespace corridorkey

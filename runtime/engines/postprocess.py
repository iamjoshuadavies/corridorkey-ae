"""
Post-processing operations for keyed output.

Adapted from EZ-CorridorKey's CorridorKeyModule/core/color_utils.py
(https://github.com/edenaion/EZ-CorridorKey).

All functions operate on numpy arrays (float32, 0-1 range).
"""

import logging

import cv2
import numpy as np

logger = logging.getLogger("corridorkey.postprocess")


def despill(
    fg_rgb: np.ndarray,
    strength: float = 0.5,
    mode: str = "average",
) -> np.ndarray:
    """Remove green spill from foreground RGB using luminance-preserving method.

    Args:
        fg_rgb: (H, W, 3) float32 RGB in [0, 1].
        strength: 0.0 (no despill) to 1.0 (full despill).
        mode: 'average' = limit is (R+B)/2, 'max' = limit is max(R, B).

    Returns:
        (H, W, 3) float32 RGB with green spill removed.
    """
    if strength <= 0.0:
        return fg_rgb

    r = fg_rgb[..., 0]
    g = fg_rgb[..., 1]
    b = fg_rgb[..., 2]

    if mode == "max":
        limit = np.maximum(r, b)
    else:
        limit = (r + b) / 2.0

    # Excess green beyond the limit
    spill_amount = np.maximum(g - limit, 0.0)

    # Remove spill from green, redistribute to R/B to preserve luminance
    g_new = g - spill_amount
    r_new = r + spill_amount * 0.5
    b_new = b + spill_amount * 0.5

    despilled = np.stack([r_new, g_new, b_new], axis=-1)

    if strength < 1.0:
        return fg_rgb * (1.0 - strength) + despilled * strength
    return despilled


def clean_matte(
    alpha: np.ndarray,
    area_threshold: int = 300,
    dilation: int = 15,
    blur_size: int = 5,
) -> np.ndarray:
    """Clean up small disconnected components from an alpha matte.

    Removes small islands (tracking markers, noise) while preserving
    the main subject boundary.

    Args:
        alpha: (H, W) float32 in [0, 1].
        area_threshold: Minimum component area in pixels to keep.
        dilation: Pixels to dilate remaining mask (elliptical kernel).
        blur_size: Gaussian blur radius for smooth edges.

    Returns:
        (H, W) float32 cleaned alpha.
    """
    # Threshold to binary
    mask_8u = (alpha > 0.5).astype(np.uint8) * 255

    # Find connected components
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(mask_8u, connectivity=8)

    # Keep only components above the area threshold
    cleaned_mask = np.zeros_like(mask_8u)
    for i in range(1, num_labels):  # Skip background (label 0)
        if stats[i, cv2.CC_STAT_AREA] >= area_threshold:
            cleaned_mask[labels == i] = 255

    # Dilate to restore edges
    if dilation > 0:
        kernel_size = int(dilation * 2 + 1)
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (kernel_size, kernel_size))
        cleaned_mask = cv2.dilate(cleaned_mask, kernel)

    # Blur for smooth transitions
    if blur_size > 0:
        b_size = int(blur_size * 2 + 1)
        cleaned_mask = cv2.GaussianBlur(cleaned_mask, (b_size, b_size), 0)

    # Convert back to float safe zone and apply
    safe_zone = cleaned_mask.astype(np.float32) / 255.0
    return alpha * safe_zone


def apply_postprocessing(
    alpha: np.ndarray,
    fg_rgb: np.ndarray,
    despill_strength: float = 0.5,
    despeckle_strength: float = 0.0,
    matte_cleanup_strength: float = 0.0,
    brightness: float = 1.0,
) -> tuple[np.ndarray, np.ndarray]:
    """Apply all post-processing to keyed output.

    Args:
        alpha: (H, W) uint8 alpha matte.
        fg_rgb: (H, W, 3) uint8 foreground RGB.
        despill_strength: 0-1, green spill removal.
        despeckle_strength: 0-1, small component removal.
        matte_cleanup_strength: 0-1, edge refinement.

    Returns:
        (alpha, fg_rgb) — both uint8, post-processed.
    """
    # Convert to float for processing
    alpha_f = alpha.astype(np.float32) / 255.0
    fg_f = fg_rgb.astype(np.float32) / 255.0

    # 1. Despeckle (connected component cleanup)
    if despeckle_strength > 0.05:
        # Scale area threshold: higher strength = more aggressive cleanup
        # 0.0 → threshold 800 (keep most), 1.0 → threshold 100 (remove more)
        area_thresh = int(800 - despeckle_strength * 700)
        dilation = int(10 + despeckle_strength * 20)
        blur = int(3 + despeckle_strength * 5)
        alpha_f = clean_matte(alpha_f, area_threshold=area_thresh,
                              dilation=dilation, blur_size=blur)

    # 2. Matte cleanup (additional edge refinement)
    if matte_cleanup_strength > 0.05:
        # Gaussian blur + levels crush for cleaner edges
        blur_sigma = 0.5 + matte_cleanup_strength * 2.0
        ksize = int(blur_sigma * 4) | 1  # Ensure odd
        if ksize >= 3:
            alpha_f = cv2.GaussianBlur(alpha_f, (ksize, ksize), blur_sigma)

        # Push towards binary (crush blacks/whites)
        low = 0.05 + matte_cleanup_strength * 0.15   # black point
        high = 0.95 - matte_cleanup_strength * 0.15   # white point
        alpha_f = np.clip((alpha_f - low) / (high - low), 0.0, 1.0)

    # 3. Despill (green removal from foreground)
    if despill_strength > 0.05:
        fg_f = despill(fg_f, strength=despill_strength, mode="average")

    # 4. Brightness (RGB multiplier, 1.0 = no change)
    if abs(brightness - 1.0) > 0.01:
        fg_f = np.clip(fg_f * brightness, 0.0, 1.0)

    # Convert back to uint8
    alpha_out = np.clip(alpha_f * 255.0, 0, 255).astype(np.uint8)
    fg_out = np.clip(fg_f * 255.0, 0, 255).astype(np.uint8)

    return alpha_out, fg_out

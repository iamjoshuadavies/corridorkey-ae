"""
PyTorch inference engine for CorridorKey.

Targets the same `CorridorKey_v1.0.pth` checkpoint that EZ-CorridorKey
ships on Windows. The model architecture (GreenFormer, defined in
EZ-CorridorKey's `_internal/CorridorKeyModule/core/model_transformer.py`)
is loaded at runtime by file path via `importlib.util` so we never
redistribute their proprietary code — we just import it from the user's
existing install.

Discovery (in order):
  1. EZ_CORRIDORKEY_PATH env var (root of an EZ-CorridorKey install)
  2. ~/Desktop/EZ-CorridorKey  (the canonical install location)
  3. %ProgramFiles%/EZ-CorridorKey

The checkpoint's position embedding is sized for 2048x2048, which is the
only spatial size the encoder accepts as-is. We pad the AE frame to
2048x2048 with replicate borders (no resize when frame fits), run the
model, and crop the result back. For inputs larger than 2048 in either
axis we downsample preserving aspect ratio first.
"""

import hashlib
import importlib.util
import logging
import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np
from numpy.typing import NDArray

from engines.base import InferenceEngine, InferenceRequest, InferenceResult

logger = logging.getLogger("corridorkey.engines.pytorch")


# Locations to check for an EZ-CorridorKey install (provides the .pth + .py).
_EZ_SEARCH_PATHS = [
    Path.home() / "Desktop" / "EZ-CorridorKey",
    Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "EZ-CorridorKey",
    Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "EZ-CorridorKey",
]


def find_ez_corridorkey_root() -> Optional[Path]:
    """Locate an EZ-CorridorKey install root (env var first, then defaults)."""
    env = os.environ.get("EZ_CORRIDORKEY_PATH")
    if env:
        p = Path(env)
        if p.exists():
            return p
        logger.warning("EZ_CORRIDORKEY_PATH set but path does not exist: %s", env)

    for p in _EZ_SEARCH_PATHS:
        if p.exists() and (p / "CorridorKeyModule").exists():
            return p
    return None


def find_pytorch_checkpoint() -> Optional[Path]:
    """Find CorridorKey_v1.0.pth inside an EZ-CorridorKey install."""
    root = find_ez_corridorkey_root()
    if root is None:
        return None
    candidates = [
        root / "CorridorKeyModule" / "checkpoints" / "CorridorKey_v1.0.pth",
    ]
    for c in candidates:
        if c.exists():
            logger.info("Found PyTorch checkpoint at: %s", c)
            return c
    return None


def _find_model_module_file() -> Optional[Path]:
    """Find model_transformer.py inside an EZ-CorridorKey install."""
    root = find_ez_corridorkey_root()
    if root is None:
        return None
    candidates = [
        root / "_internal" / "CorridorKeyModule" / "core" / "model_transformer.py",
        root / "CorridorKeyModule" / "core" / "model_transformer.py",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def _load_greenformer_class():
    """Load the GreenFormer class from model_transformer.py at runtime.

    Uses importlib.util to import directly by file path so we never copy
    or redistribute EZ-CorridorKey's source.
    """
    module_file = _find_model_module_file()
    if module_file is None:
        raise FileNotFoundError(
            "Could not find model_transformer.py. Set EZ_CORRIDORKEY_PATH "
            "or install EZ-CorridorKey to ~/Desktop/EZ-CorridorKey."
        )

    # The module imports timm at top — that's fine, we have it.
    spec = importlib.util.spec_from_file_location(
        "_corridorkey_greenformer", str(module_file)
    )
    mod = importlib.util.module_from_spec(spec)
    sys.modules["_corridorkey_greenformer"] = mod
    spec.loader.exec_module(mod)
    return mod.GreenFormer


# Architecture constant — encoder pos_embed is sized for this. Don't change
# without retraining or resizing pos_embed.
MODEL_IMG_SIZE = 2048

# ImageNet normalization — the Hiera backbone expects RGB normalized this way.
# Matches what corridorkey-mlx's preprocess_mlx.py does. Skipping this is what
# was producing the "milky" foreground.
_IMAGENET_MEAN = (0.485, 0.456, 0.406)
_IMAGENET_STD  = (0.229, 0.224, 0.225)


class PyTorchEngine(InferenceEngine):
    """CorridorKey inference engine using PyTorch.

    Targets CUDA when available (works great on RTX 4090 — ~400ms at
    2048x2048 fp16) and falls back to CPU (slow — for development only).
    """

    def __init__(self, use_refiner: bool = True, prefer_fp16: bool = True) -> None:
        self._model = None
        self._device = None
        self._dtype = None
        self._use_refiner = use_refiner
        self._prefer_fp16 = prefer_fp16
        self._model_path: Optional[str] = None

        # Raw model output cache — same idea as MLXEngine. Avoids re-running
        # inference when only the postprocessing sliders move.
        self._raw_cache_key: Optional[str] = None
        self._raw_cache_alpha: Optional[np.ndarray] = None
        self._raw_cache_fg: Optional[np.ndarray] = None

    @property
    def device_name(self) -> str:
        if self._device is None:
            return "uninitialized"
        if self._device.type == "cuda":
            import torch
            return f"CUDA: {torch.cuda.get_device_name(self._device)}"
        return "CPU"

    def is_ready(self) -> bool:
        return self._model is not None

    def load_model(self, model_path: str) -> None:
        import torch

        self._model_path = model_path
        GreenFormer = _load_greenformer_class()

        logger.info("Building GreenFormer (img_size=%d)", MODEL_IMG_SIZE)
        model = GreenFormer(img_size=MODEL_IMG_SIZE, use_refiner=self._use_refiner)

        logger.info("Loading checkpoint: %s", model_path)
        ckpt = torch.load(model_path, map_location="cpu", weights_only=False)
        sd = ckpt.get("state_dict", ckpt)
        # Strip torch.compile prefix
        sd = {k.replace("_orig_mod.", "", 1): v for k, v in sd.items()}
        missing, unexpected = model.load_state_dict(sd, strict=True)
        logger.info(
            "Loaded checkpoint: %d params, missing=%d, unexpected=%d",
            sum(p.numel() for p in model.parameters()), len(missing), len(unexpected),
        )

        # Pick device + dtype
        if torch.cuda.is_available():
            self._device = torch.device("cuda")
            self._dtype = torch.float16 if self._prefer_fp16 else torch.float32
        else:
            self._device = torch.device("cpu")
            self._dtype = torch.float32  # fp16 on CPU is slow + unsupported by some ops
            logger.warning("CUDA not available — falling back to CPU (slow)")

        model = model.to(device=self._device, dtype=self._dtype).eval()
        self._model = model
        logger.info("PyTorch model ready on %s (%s)", self.device_name, self._dtype)

    def unload(self) -> None:
        import torch
        self._model = None
        self._raw_cache_key = None
        self._raw_cache_alpha = None
        self._raw_cache_fg = None
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    # --- Inference helpers ---

    def _prepare_input(
        self, rgb: np.ndarray, alpha_hint: np.ndarray
    ) -> "tuple[any, tuple[int, int, int, int], tuple[int, int]]":
        """Convert HxWx3 RGB + HxW hint to a model input tensor at MODEL_IMG_SIZE.

        Returns:
            input_tensor: torch.Tensor[1, 4, MODEL_IMG_SIZE, MODEL_IMG_SIZE]
            crop_box:     (top, left, work_h, work_w) — region of the model
                          output that contains real (non-padded) pixels at
                          the model's canvas resolution
            input_size:   (orig_h, orig_w)
        """
        import torch
        import torch.nn.functional as F

        h, w = rgb.shape[:2]

        # rgb HxWx3 uint8 -> 1x3xHxW float in [0,1], then ImageNet-normalize.
        # The Hiera backbone was trained with ImageNet stats; skipping this
        # produces a washed-out / "milky" foreground in the model output
        # (because the backbone sees the wrong input distribution).
        # Reference: corridorkey_mlx/io/preprocess_mlx.py applies the same
        # mean/std to RGB and leaves the hint un-normalized.
        rgb_t = torch.from_numpy(rgb).permute(2, 0, 1).unsqueeze(0).float() / 255.0
        mean = torch.tensor(_IMAGENET_MEAN).view(1, 3, 1, 1)
        std  = torch.tensor(_IMAGENET_STD).view(1, 3, 1, 1)
        rgb_t = (rgb_t - mean) / std
        # Hint is a mask, not color data — leave un-normalized in [0,1].
        hint_t = torch.from_numpy(alpha_hint).unsqueeze(0).unsqueeze(0).float() / 255.0
        x = torch.cat([rgb_t, hint_t], dim=1)

        # Step 1: if larger than MODEL_IMG_SIZE in either dim, scale down
        # preserving aspect ratio so the longer side == MODEL_IMG_SIZE.
        if max(h, w) > MODEL_IMG_SIZE:
            scale = MODEL_IMG_SIZE / max(h, w)
            new_h = max(1, int(round(h * scale)))
            new_w = max(1, int(round(w * scale)))
            x = F.interpolate(x, size=(new_h, new_w), mode="bilinear", align_corners=False)
            work_h, work_w = new_h, new_w
        else:
            work_h, work_w = h, w

        # Step 2: pad to MODEL_IMG_SIZExMODEL_IMG_SIZE, centered, replicate border
        pad_left = (MODEL_IMG_SIZE - work_w) // 2
        pad_right = MODEL_IMG_SIZE - work_w - pad_left
        pad_top = (MODEL_IMG_SIZE - work_h) // 2
        pad_bottom = MODEL_IMG_SIZE - work_h - pad_top
        x = F.pad(x, (pad_left, pad_right, pad_top, pad_bottom), mode="replicate")

        # Move to device + dtype
        x = x.to(device=self._device, dtype=self._dtype, non_blocking=True)

        return x, (pad_top, pad_left, work_h, work_w), (h, w)

    def _crop_and_unpad(
        self, output: "any", crop_box: "tuple[int,int,int,int]", original: "tuple[int,int]"
    ) -> "tuple[np.ndarray, np.ndarray]":
        """Crop padded model output back to the original frame dimensions."""
        import torch
        import torch.nn.functional as F

        pad_top, pad_left, work_h, work_w = crop_box
        orig_h, orig_w = original

        alpha = output["alpha"][:, :, pad_top:pad_top + work_h, pad_left:pad_left + work_w]
        fg = output["fg"][:, :, pad_top:pad_top + work_h, pad_left:pad_left + work_w]

        # If we downsampled in _prepare_input, upsample back to original size
        if (work_h, work_w) != (orig_h, orig_w):
            alpha = F.interpolate(alpha, size=(orig_h, orig_w), mode="bilinear", align_corners=False)
            fg = F.interpolate(fg, size=(orig_h, orig_w), mode="bilinear", align_corners=False)

        # No transfer function on output — the model is trained input-domain
        # to output-domain, so sRGB-in == sRGB-out.
        fg = fg.float()
        alpha = alpha.float()

        # Tensor -> numpy uint8
        alpha_np = (alpha.clamp(0, 1) * 255.0).byte().cpu().numpy()[0, 0]  # HxW
        fg_np = (fg.clamp(0, 1) * 255.0).byte().cpu().numpy()[0]            # 3xHxW
        fg_np = np.transpose(fg_np, (1, 2, 0))                              # HxWx3
        return alpha_np, fg_np

    def process(self, request: InferenceRequest) -> InferenceResult:
        if not self.is_ready():
            return InferenceResult(image=request.image, success=False, error="Model not loaded")

        try:
            import torch

            argb = request.image
            h, w = argb.shape[:2]

            # ARGB -> RGB
            rgb = np.zeros((h, w, 3), dtype=np.uint8)
            rgb[:, :, 0] = argb[:, :, 1]  # R
            rgb[:, :, 1] = argb[:, :, 2]  # G
            rgb[:, :, 2] = argb[:, :, 3]  # B

            # Alpha hint — full white if not provided (model expects 4ch input)
            alpha_hint = getattr(request, "_alpha_hint", None)
            if alpha_hint is None:
                alpha_hint = np.full((h, w), 255, dtype=np.uint8)
            else:
                logger.info("Using external alpha hint (%dx%d)",
                            alpha_hint.shape[1], alpha_hint.shape[0])

            # Cache key for raw model output (same scheme as MLXEngine)
            pixel_sample = rgb[:4].tobytes() + rgb[-4:].tobytes()
            hint_sample = alpha_hint[:4].tobytes() + alpha_hint[-4:].tobytes()
            raw_key = hashlib.md5(
                pixel_sample + hint_sample
                + f":{request.refiner:.3f}:{h}:{w}".encode()
            ).hexdigest()

            if raw_key == self._raw_cache_key and self._raw_cache_alpha is not None:
                alpha = self._raw_cache_alpha.copy()
                fg = self._raw_cache_fg.copy()
                logger.info("Raw model cache HIT — skipping inference")
            else:
                x, crop_box, orig_size = self._prepare_input(rgb, alpha_hint)

                with torch.no_grad():
                    refiner_scale = (
                        torch.tensor(request.refiner, device=self._device, dtype=self._dtype)
                        if self._use_refiner else None
                    )
                    output = self._model(x, refiner_scale=refiner_scale)

                alpha, fg = self._crop_and_unpad(output, crop_box, orig_size)

                self._raw_cache_key = raw_key
                self._raw_cache_alpha = alpha.copy()
                self._raw_cache_fg = fg.copy()
                logger.info("Inference complete, raw output cached")

            # Postprocessing (despill, despeckle, cleanup) — cpu-side, cheap
            from engines.postprocess import apply_postprocessing
            alpha, fg = apply_postprocessing(
                alpha, fg,
                despill_strength=request.despill,
                despeckle_strength=request.despeckle,
                matte_cleanup_strength=request.matte_cleanup,
            )

            # Composite for mode 3
            alpha_3ch = alpha[:, :, np.newaxis].astype(np.float32) / 255.0
            comp = (fg.astype(np.float32) * alpha_3ch).astype(np.uint8)

            output_argb = np.zeros((h, w, 4), dtype=np.uint8)
            if request.output_mode == 0:
                # Processed: foreground premultiplied with alpha for AE compositing
                output_argb[:, :, 0] = alpha
                output_argb[:, :, 1] = fg[:, :, 0]
                output_argb[:, :, 2] = fg[:, :, 1]
                output_argb[:, :, 3] = fg[:, :, 2]
            elif request.output_mode == 1:
                # Matte: alpha as grayscale
                output_argb[:, :, 0] = 255
                output_argb[:, :, 1] = alpha
                output_argb[:, :, 2] = alpha
                output_argb[:, :, 3] = alpha
            elif request.output_mode == 2:
                # Foreground unmultiplied
                output_argb[:, :, 0] = 255
                output_argb[:, :, 1] = fg[:, :, 0]
                output_argb[:, :, 2] = fg[:, :, 1]
                output_argb[:, :, 3] = fg[:, :, 2]
            elif request.output_mode == 3:
                # Composite over black
                output_argb[:, :, 0] = 255
                output_argb[:, :, 1] = comp[:, :, 0]
                output_argb[:, :, 2] = comp[:, :, 1]
                output_argb[:, :, 3] = comp[:, :, 2]
            else:
                output_argb = argb

            return InferenceResult(image=output_argb, success=True)

        except Exception as e:
            logger.exception("PyTorch inference failed")
            return InferenceResult(image=request.image, success=False, error=str(e))

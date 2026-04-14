"""
PyTorch inference engine for CorridorKey.

Self-contained: uses the vendored GreenFormer in `_greenformer.py` and
loads weights via `_weights_loader.py` (which can pull from a local .pth,
a cached download, or download the official MLX safetensors and convert).

Quality dropdown wiring:
    quality 0 (Fastest, 256)        -> 512x512 model, refiner skipped
    quality 1 (Fast, 512)           -> 512x512 model, refiner enabled
    quality 2 (High, 1024)          -> 1024x1024 model, refiner enabled
    quality 3 (Full Res, Tiled)     -> 2048x2048 model, refiner enabled

Models at different sizes are built lazily on first use and cached. They
share weights via pos_embed bicubic interpolation (the only spatial param
that depends on input resolution). On a 4090, all four sizes use ~1.5 GB
of VRAM together, well within budget.
"""

from __future__ import annotations

import hashlib
import logging
from pathlib import Path
from typing import TYPE_CHECKING, Any

import numpy as np

from engines._greenformer import GreenFormer, load_state_dict_into
from engines._weights_loader import load_pytorch_state_dict, resolve_pytorch_weights
from engines.base import InferenceEngine, InferenceRequest, InferenceResult

if TYPE_CHECKING:
    import torch

logger = logging.getLogger("corridorkey.engines.pytorch")


# ImageNet normalization — the Hiera backbone needs RGB normalized this way.
# Skipping it produces a "milky" / washed-out foreground because the backbone
# sees the wrong input distribution.
_IMAGENET_MEAN = (0.485, 0.456, 0.406)
_IMAGENET_STD  = (0.229, 0.224, 0.225)


# Map AE plugin quality_mode -> (model img_size, skip_refiner)
_QUALITY_PROFILES = {
    0: (512,  True),    # Fastest — small model, no refiner
    1: (512,  False),   # Fast    — small model, with refiner
    2: (1024, False),   # High    — medium model, with refiner
    3: (2048, False),   # Full    — full model, with refiner
}
_DEFAULT_QUALITY = 3


def _quality_profile(quality_mode: int) -> tuple[int, bool]:
    return _QUALITY_PROFILES.get(quality_mode, _QUALITY_PROFILES[_DEFAULT_QUALITY])


class PyTorchEngine(InferenceEngine):
    """CorridorKey inference via PyTorch.

    Targets CUDA when available (RTX 4090: ~400ms at 2048×2048 fp16) and
    falls back to CPU. Multiple model resolutions are cached so the
    Quality dropdown can swap between them without reloading weights.
    """

    def __init__(self, prefer_fp16: bool = True) -> None:
        self._device: torch.device | None = None
        self._dtype: torch.dtype | None = None
        self._prefer_fp16 = prefer_fp16
        self._raw_state_dict: dict[str, Any] | None = None   # CPU-resident, cached for rebuilds
        self._models: dict[int, GreenFormer] = {}            # img_size -> built model on device
        self._model_path: str | None = None

        # Raw model output cache — same as MLXEngine.
        self._raw_cache_key: str | None = None
        self._raw_cache_alpha: np.ndarray | None = None
        self._raw_cache_fg: np.ndarray | None = None

    @property
    def device_name(self) -> str:
        if self._device is None:
            return "uninitialized"
        if self._device.type == "cuda":
            import torch
            return f"CUDA: {torch.cuda.get_device_name(self._device)}"
        return "CPU"

    def is_ready(self) -> bool:
        return self._raw_state_dict is not None

    def load_model(self, model_path: str) -> None:
        """Load the checkpoint into a CPU-resident state_dict.

        Per-size GreenFormer instances are built lazily on first use.
        """
        import torch

        if model_path:
            # Caller passed an explicit path
            self._model_path = model_path
            fmt = "safetensors" if model_path.endswith(".safetensors") else "pth"
            sd = load_pytorch_state_dict(Path(model_path), fmt)
        else:
            located = resolve_pytorch_weights()
            if located is None:
                raise FileNotFoundError(
                    "No CorridorKey weights found and download failed. "
                    "Set CORRIDORKEY_PT_WEIGHTS to a local .pth or "
                    ".safetensors checkpoint, or check your network "
                    "connection and retry."
                )
            path, fmt = located
            self._model_path = str(path)
            sd = load_pytorch_state_dict(path, fmt)

        self._raw_state_dict = sd

        if torch.cuda.is_available():
            self._device = torch.device("cuda")
            self._dtype = torch.float16 if self._prefer_fp16 else torch.float32
        else:
            self._device = torch.device("cpu")
            self._dtype = torch.float32
            logger.warning("CUDA not available — falling back to CPU (slow)")

        logger.info(
            "PyTorch weights loaded (%d tensors, source=%s) — device=%s dtype=%s",
            len(sd), self._model_path, self.device_name, self._dtype,
        )

    def unload(self) -> None:
        import torch
        self._models.clear()
        self._raw_state_dict = None
        self._raw_cache_key = None
        self._raw_cache_alpha = None
        self._raw_cache_fg = None
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    # --- Per-size model cache ---

    def _get_model(self, img_size: int) -> GreenFormer:
        """Build (or return cached) GreenFormer at the requested img_size."""
        cached = self._models.get(img_size)
        if cached is not None:
            return cached

        import torch
        assert self._raw_state_dict is not None

        logger.info("Building GreenFormer at img_size=%d", img_size)
        model = GreenFormer(img_size=img_size)
        missing, unexpected = load_state_dict_into(model, self._raw_state_dict)
        if unexpected:
            logger.warning("Unexpected weight keys ignored: %s", unexpected[:5])
        # Missing num_batches_tracked is expected when loading converted MLX
        # weights — BatchNorm tolerates it.
        model = model.to(device=self._device, dtype=self._dtype).eval()

        self._models[img_size] = model
        if torch.cuda.is_available():
            logger.info(
                "GPU mem after build: %.2f GB",
                torch.cuda.memory_allocated() / 1e9,
            )
        return model

    # --- Pad / crop helpers ---

    def _prepare_input(
        self, rgb: np.ndarray, alpha_hint: np.ndarray, model_img_size: int
    ) -> tuple[torch.Tensor, tuple[int, int, int, int], tuple[int, int]]:
        """Build a [1, 4, model_img_size, model_img_size] tensor from RGB + hint.

        Returns the tensor, the (top, left, work_h, work_w) crop box of the
        non-padded region at the model's working resolution, and the original
        (h, w) of the input frame.
        """
        import torch
        import torch.nn.functional as F  # noqa: N812  (PyTorch convention)

        h, w = rgb.shape[:2]

        rgb_t = torch.from_numpy(rgb).permute(2, 0, 1).unsqueeze(0).float() / 255.0
        # ImageNet normalize RGB only (hint is a mask, stays in [0,1])
        mean = torch.tensor(_IMAGENET_MEAN).view(1, 3, 1, 1)
        std  = torch.tensor(_IMAGENET_STD).view(1, 3, 1, 1)
        rgb_t = (rgb_t - mean) / std
        hint_t = torch.from_numpy(alpha_hint).unsqueeze(0).unsqueeze(0).float() / 255.0
        x = torch.cat([rgb_t, hint_t], dim=1)

        # Step 1: if larger than the model size in either dim, scale down
        # preserving aspect ratio.
        if max(h, w) > model_img_size:
            scale = model_img_size / max(h, w)
            new_h = max(1, int(round(h * scale)))
            new_w = max(1, int(round(w * scale)))
            x = F.interpolate(x, size=(new_h, new_w), mode="bilinear", align_corners=False)
            work_h, work_w = new_h, new_w
        else:
            work_h, work_w = h, w

        # Step 2: center-pad to a square of model_img_size with replicate
        # borders. (Replicate avoids introducing edges the matter-detector
        # might react to.)
        pad_left = (model_img_size - work_w) // 2
        pad_right = model_img_size - work_w - pad_left
        pad_top = (model_img_size - work_h) // 2
        pad_bottom = model_img_size - work_h - pad_top
        x = F.pad(x, (pad_left, pad_right, pad_top, pad_bottom), mode="replicate")

        x = x.to(device=self._device, dtype=self._dtype, non_blocking=True)
        return x, (pad_top, pad_left, work_h, work_w), (h, w)

    def _crop_and_unpad(
        self,
        output: dict[str, torch.Tensor],
        crop_box: tuple[int, int, int, int],
        original: tuple[int, int],
    ) -> tuple[np.ndarray, np.ndarray]:
        """Crop padded model output back to the original frame dimensions."""
        import torch.nn.functional as F  # noqa: N812  (PyTorch convention)

        pad_top, pad_left, work_h, work_w = crop_box
        orig_h, orig_w = original

        alpha = output["alpha"][:, :, pad_top:pad_top + work_h, pad_left:pad_left + work_w]
        fg    = output["fg"][:, :, pad_top:pad_top + work_h, pad_left:pad_left + work_w]

        if (work_h, work_w) != (orig_h, orig_w):
            alpha = F.interpolate(
                alpha, size=(orig_h, orig_w), mode="bilinear", align_corners=False,
            )
            fg = F.interpolate(
                fg, size=(orig_h, orig_w), mode="bilinear", align_corners=False,
            )

        alpha = alpha.float()
        fg    = fg.float()

        alpha_np = (alpha.clamp(0, 1) * 255.0).byte().cpu().numpy()[0, 0]   # HxW
        fg_np    = (fg.clamp(0, 1) * 255.0).byte().cpu().numpy()[0]          # 3xHxW
        fg_np    = np.transpose(fg_np, (1, 2, 0))                            # HxWx3
        return alpha_np, fg_np

    # --- Main entry point ---

    def process(self, request: InferenceRequest) -> InferenceResult:
        if not self.is_ready():
            return InferenceResult(image=request.image, success=False, error="Model not loaded")

        try:
            import torch

            argb = request.image
            h, w = argb.shape[:2]

            # ARGB -> RGB
            rgb = np.zeros((h, w, 3), dtype=np.uint8)
            rgb[:, :, 0] = argb[:, :, 1]
            rgb[:, :, 1] = argb[:, :, 2]
            rgb[:, :, 2] = argb[:, :, 3]

            alpha_hint = getattr(request, "_alpha_hint", None)
            if alpha_hint is None:
                alpha_hint = np.full((h, w), 255, dtype=np.uint8)
            else:
                logger.info("Using external alpha hint (%dx%d)",
                            alpha_hint.shape[1], alpha_hint.shape[0])

            # Quality-mode dispatch — handler.py stashes the quality on the
            # request via a `_quality_mode` attr. Fall back to the default
            # (Full Res) if a caller forgets to set it.
            quality_mode = getattr(request, "_quality_mode", _DEFAULT_QUALITY)
            model_img_size, skip_refiner = _quality_profile(quality_mode)

            # Cache key for raw model output. Hash the FULL buffers — sampling
            # only first/last few rows misses mid-frame edits to the hint mask
            # (which is where keying actually matters).
            pix_md5  = hashlib.md5(rgb.tobytes()).hexdigest()
            hint_md5 = hashlib.md5(alpha_hint.tobytes()).hexdigest()
            raw_key = (
                f"{pix_md5}:{hint_md5}:{request.refiner:.3f}:{h}:{w}:{quality_mode}"
            )

            if (
                raw_key == self._raw_cache_key
                and self._raw_cache_alpha is not None
                and self._raw_cache_fg is not None
            ):
                alpha = self._raw_cache_alpha.copy()
                fg = self._raw_cache_fg.copy()
                logger.info("Raw model cache HIT — skipping inference")
            else:
                model = self._get_model(model_img_size)
                x, crop_box, orig_size = self._prepare_input(rgb, alpha_hint, model_img_size)

                with torch.no_grad():
                    refiner_scale = (
                        torch.tensor(request.refiner, device=self._device, dtype=self._dtype)
                        if not skip_refiner else None
                    )
                    output = model(x, refiner_scale=refiner_scale, skip_refiner=skip_refiner)

                alpha, fg = self._crop_and_unpad(output, crop_box, orig_size)

                self._raw_cache_key = raw_key
                self._raw_cache_alpha = alpha.copy()
                self._raw_cache_fg = fg.copy()
                logger.info(
                    "Inference complete (img_size=%d skip_refiner=%s) — cached",
                    model_img_size, skip_refiner,
                )

            # Postprocessing
            from engines.postprocess import apply_postprocessing
            alpha, fg = apply_postprocessing(
                alpha, fg,
                despill_strength=request.despill,
                despeckle_strength=request.despeckle,
                matte_cleanup_strength=request.matte_cleanup,
            )

            alpha_3ch = alpha[:, :, np.newaxis].astype(np.float32) / 255.0
            comp = (fg.astype(np.float32) * alpha_3ch).astype(np.uint8)

            output_argb = np.zeros((h, w, 4), dtype=np.uint8)
            if request.output_mode == 0:
                output_argb[:, :, 0] = alpha
                output_argb[:, :, 1] = fg[:, :, 0]
                output_argb[:, :, 2] = fg[:, :, 1]
                output_argb[:, :, 3] = fg[:, :, 2]
            elif request.output_mode == 1:
                output_argb[:, :, 0] = 255
                output_argb[:, :, 1] = alpha
                output_argb[:, :, 2] = alpha
                output_argb[:, :, 3] = alpha
            elif request.output_mode == 2:
                output_argb[:, :, 0] = 255
                output_argb[:, :, 1] = fg[:, :, 0]
                output_argb[:, :, 2] = fg[:, :, 1]
                output_argb[:, :, 3] = fg[:, :, 2]
            elif request.output_mode == 3:
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



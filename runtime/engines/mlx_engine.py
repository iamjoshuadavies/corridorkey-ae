"""
MLX inference engine — wraps corridorkey_mlx for Apple Silicon.

Uses the CorridorKeyMLXEngine from the corridorkey-mlx package
to perform physically accurate green-screen keying via MLX.
"""

import logging
import os
import platform
from pathlib import Path
from typing import Optional

import numpy as np
from numpy.typing import NDArray

from engines.base import InferenceEngine, InferenceRequest, InferenceResult

logger = logging.getLogger("corridorkey.engines.mlx")

# Known model weight locations (checked in order)
MODEL_SEARCH_PATHS = [
    # Our own cache
    Path.home() / "Library" / "Application Support" / "CorridorKey" / "models" / "corridorkey_mlx.safetensors",
    # EZ-CorridorKey's install
    Path.home() / "Library" / "Application Support" / "EZ-CorridorKey" / "CorridorKeyModule" / "checkpoints" / "corridorkey_mlx.safetensors",
    # App bundle location
    Path("/Applications/EZ-CorridorKey.app/Contents/MacOS/CorridorKeyModule/checkpoints/corridorkey_mlx.safetensors"),
]


def find_model_weights() -> Optional[Path]:
    """Search for corridorkey_mlx.safetensors in known locations."""
    for path in MODEL_SEARCH_PATHS:
        if path.exists():
            logger.info("Found model weights at: %s", path)
            return path
    logger.info("No model weights found locally — attempting download...")
    return download_model_weights()


def download_model_weights() -> Optional[Path]:
    """Download model weights from the corridorkey-mlx GitHub release."""
    try:
        from corridorkey_mlx.weights import download_weights

        # Download to our own cache location
        dest = Path.home() / "Library" / "Application Support" / "CorridorKey" / "models"
        dest.mkdir(parents=True, exist_ok=True)

        logger.info("Downloading CorridorKey model weights to %s ...", dest)
        path = download_weights(out=dest)
        logger.info("Model weights downloaded: %s", path)
        return path

    except ImportError:
        logger.error("corridorkey_mlx.weights not available for download")
        return None
    except Exception:
        logger.exception("Failed to download model weights")
        return None


class MLXEngine(InferenceEngine):
    """CorridorKey inference engine using MLX (Apple Silicon).

    Switches between tiled and direct mode on demand.
    Only one engine instance in memory at a time to conserve RAM.
    Reinitializes when the mode changes.

    Caches raw model output (alpha + fg) so that changing post-processing
    params (despill, despeckle, matte cleanup) doesn't re-run inference.
    """

    def __init__(
        self,
        tile_size: int = 512,
        overlap: int = 64,
        use_refiner: bool = True,
    ) -> None:
        self._engine = None
        self._model_path: Optional[str] = None
        self._tile_size = tile_size
        self._overlap = overlap
        self._use_refiner = use_refiner
        self._current_mode_tiled: Optional[bool] = None  # Track current mode

        # Raw model output cache: avoids re-running inference when only
        # post-processing params change. Keyed by (pixel_hash, refiner, tiled).
        self._raw_cache_key: Optional[str] = None
        self._raw_cache_alpha: Optional[np.ndarray] = None
        self._raw_cache_fg: Optional[np.ndarray] = None

    def load_model(self, model_path: str) -> None:
        """Load the CorridorKey MLX model in tiled mode (default)."""
        self._model_path = model_path
        self._init_engine(tiled=True)

    def _init_engine(self, tiled: bool) -> None:
        """Initialize or reinitialize the engine in the specified mode."""
        from corridorkey_mlx import CorridorKeyMLXEngine

        if self._current_mode_tiled == tiled and self._engine is not None:
            return  # Already in the right mode

        # Release old engine
        self._engine = None

        mode_str = "tiled" if tiled else "direct"
        logger.info("Initializing MLX engine (%s mode, tile_size=%d)", mode_str, self._tile_size)

        if tiled:
            self._engine = CorridorKeyMLXEngine(
                checkpoint_path=self._model_path,
                img_size=self._tile_size,
                tile_size=self._tile_size,
                overlap=self._overlap,
                use_refiner=self._use_refiner,
                compile=False,
            )
        else:
            self._engine = CorridorKeyMLXEngine(
                checkpoint_path=self._model_path,
                img_size=self._tile_size,
                use_refiner=self._use_refiner,
                compile=False,
            )

        self._current_mode_tiled = tiled
        logger.info("CorridorKey MLX model loaded successfully")

    def is_ready(self) -> bool:
        return self._engine is not None and self._model_path is not None

    def process(self, request: InferenceRequest) -> InferenceResult:
        """Process a frame through CorridorKey MLX inference."""
        if not self.is_ready():
            return InferenceResult(
                image=request.image,
                success=False,
                error="Model not loaded",
            )

        try:
            # Switch engine mode if needed: tiled for quality 0, direct for 1-3
            use_tiled = not getattr(request, '_use_direct', False)
            self._init_engine(tiled=use_tiled)

            # Input is (H, W, 4) ARGB uint8 from AE
            argb = request.image
            h, w = argb.shape[:2]

            # ARGB → RGB for model input
            rgb = np.zeros((h, w, 3), dtype=np.uint8)
            rgb[:, :, 0] = argb[:, :, 1]  # R
            rgb[:, :, 1] = argb[:, :, 2]  # G
            rgb[:, :, 2] = argb[:, :, 3]  # B

            # Build cache key for raw model output: pixel content + refiner + mode
            # Post-processing params (despill, despeckle, cleanup) are NOT in this key
            import hashlib
            pixel_sample = rgb[:4].tobytes() + rgb[-4:].tobytes()
            raw_key = hashlib.md5(
                pixel_sample + f":{request.refiner:.3f}:{use_tiled}:{h}:{w}".encode()
            ).hexdigest()

            # Check raw cache — skip inference if same frame + refiner
            if raw_key == self._raw_cache_key and self._raw_cache_alpha is not None:
                alpha = self._raw_cache_alpha.copy()
                fg = self._raw_cache_fg.copy()
                logger.info("Raw model cache HIT — skipping inference, applying post-processing only")
            else:
                # Alpha hint
                alpha_hint = getattr(request, '_alpha_hint', None)
                if alpha_hint is None:
                    alpha_hint = np.full((h, w), 255, dtype=np.uint8)
                else:
                    logger.info("Using external alpha hint (%dx%d)", alpha_hint.shape[1], alpha_hint.shape[0])

                # Run inference
                result = self._engine.process_frame(
                    image=rgb,
                    mask_linear=alpha_hint,
                    refiner_scale=request.refiner,
                    despill_strength=0.0,
                    auto_despeckle=False,
                )

                alpha = result["alpha"]     # (H, W) uint8
                fg = result["fg"]          # (H, W, 3) uint8

                # Cache raw output
                self._raw_cache_key = raw_key
                self._raw_cache_alpha = alpha.copy()
                self._raw_cache_fg = fg.copy()
                logger.info("Raw model output cached")

            # Apply post-processing (cheap — ~10ms)
            from engines.postprocess import apply_postprocessing
            alpha, fg = apply_postprocessing(
                alpha, fg,
                despill_strength=request.despill,
                despeckle_strength=request.despeckle,
                matte_cleanup_strength=request.matte_cleanup,
                brightness=request.brightness,
            )

            # Recompute composite after postprocessing
            alpha_3ch = alpha[:, :, np.newaxis].astype(np.float32) / 255.0
            fg_float = fg.astype(np.float32)
            comp = (fg_float * alpha_3ch).astype(np.uint8)

            # Debug: save intermediate results (enable with CK_DEBUG=1 env var)
            if os.environ.get("CK_DEBUG"):
                from PIL import Image as _PILImage
                _PILImage.fromarray(alpha, "L").save("/tmp/ck_debug_alpha.png")
                _PILImage.fromarray(fg, "RGB").save("/tmp/ck_debug_fg.png")
                _PILImage.fromarray(comp, "RGB").save("/tmp/ck_debug_comp.png")
                _PILImage.fromarray(rgb, "RGB").save("/tmp/ck_debug_input.png")
                _PILImage.fromarray(alpha_hint, "L").save("/tmp/ck_debug_hint.png")
                logger.info("Debug images saved to /tmp/ck_debug_*.png")

            output_argb = np.zeros((h, w, 4), dtype=np.uint8)

            if request.output_mode == 0:
                # Processed: foreground with alpha (for AE compositing)
                output_argb[:, :, 0] = alpha            # A
                output_argb[:, :, 1] = fg[:, :, 0]     # R
                output_argb[:, :, 2] = fg[:, :, 1]     # G
                output_argb[:, :, 3] = fg[:, :, 2]     # B

            elif request.output_mode == 1:
                # Matte: alpha channel as grayscale, fully opaque
                output_argb[:, :, 0] = 255              # A
                output_argb[:, :, 1] = alpha            # R = alpha
                output_argb[:, :, 2] = alpha            # G = alpha
                output_argb[:, :, 3] = alpha            # B = alpha

            elif request.output_mode == 2:
                # Foreground: straight (unmultiplied) foreground, fully opaque
                output_argb[:, :, 0] = 255              # A
                output_argb[:, :, 1] = fg[:, :, 0]     # R
                output_argb[:, :, 2] = fg[:, :, 1]     # G
                output_argb[:, :, 3] = fg[:, :, 2]     # B

            elif request.output_mode == 3:
                # Composite: foreground over black, fully opaque
                output_argb[:, :, 0] = 255              # A
                output_argb[:, :, 1] = comp[:, :, 0]   # R
                output_argb[:, :, 2] = comp[:, :, 1]   # G
                output_argb[:, :, 3] = comp[:, :, 2]   # B

            else:
                # Unknown mode: passthrough
                output_argb = argb

            return InferenceResult(image=output_argb, success=True)

        except Exception as e:
            logger.exception("MLX inference failed")
            return InferenceResult(
                image=request.image,
                success=False,
                error=str(e),
            )

    def unload(self) -> None:
        self._engine = None
        self._current_mode_tiled = None
        self._model_path = None

    @property
    def device_name(self) -> str:
        return f"MLX ({platform.processor() or 'Apple Silicon'})"

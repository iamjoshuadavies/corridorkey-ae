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

    Uses tiled inference by default for full-resolution output.
    Tiles are processed at tile_size×tile_size with overlap blending.
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

    def load_model(self, model_path: str) -> None:
        """Load the CorridorKey MLX model from a safetensors checkpoint."""
        from corridorkey_mlx import CorridorKeyMLXEngine

        logger.info(
            "Loading CorridorKey MLX model from: %s (tile_size=%d, overlap=%d, refiner=%s)",
            model_path, self._tile_size, self._overlap, self._use_refiner,
        )

        self._engine = CorridorKeyMLXEngine(
            checkpoint_path=model_path,
            img_size=self._tile_size,
            tile_size=self._tile_size,
            overlap=self._overlap,
            use_refiner=self._use_refiner,
            compile=False,  # Compile not used in tiled mode
        )
        self._model_path = model_path
        logger.info("CorridorKey MLX model loaded successfully")

    def is_ready(self) -> bool:
        return self._engine is not None

    def process(self, request: InferenceRequest) -> InferenceResult:
        """Process a frame through CorridorKey MLX inference."""
        if not self.is_ready():
            return InferenceResult(
                image=request.image,
                success=False,
                error="Model not loaded",
            )

        try:
            # Input is (H, W, 4) ARGB uint8 from AE
            argb = request.image
            h, w = argb.shape[:2]

            # ARGB → RGB for model input
            rgb = np.zeros((h, w, 3), dtype=np.uint8)
            rgb[:, :, 0] = argb[:, :, 1]  # R
            rgb[:, :, 1] = argb[:, :, 2]  # G
            rgb[:, :, 2] = argb[:, :, 3]  # B

            # Alpha hint: use external hint from AE if available, else white (full frame)
            alpha_hint = getattr(request, '_alpha_hint', None)
            if alpha_hint is None:
                alpha_hint = np.full((h, w), 255, dtype=np.uint8)
                logger.debug("No alpha hint provided — using full-frame white mask")
            else:
                logger.info("Using external alpha hint (%dx%d)", alpha_hint.shape[1], alpha_hint.shape[0])

            # Run inference
            result = self._engine.process_frame(
                image=rgb,
                mask_linear=alpha_hint,
                refiner_scale=request.refiner,
                despill_strength=request.despill,
                auto_despeckle=request.despeckle > 0.1,
            )

            # Build output based on requested mode
            alpha = result["alpha"]     # (H, W) uint8
            fg = result["fg"]          # (H, W, 3) uint8
            comp = result["comp"]      # (H, W, 3) uint8

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
        self._model_path = None

    @property
    def device_name(self) -> str:
        return f"MLX ({platform.processor() or 'Apple Silicon'})"

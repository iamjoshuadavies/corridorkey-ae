"""
Request handler — dispatches incoming IPC messages to the appropriate action.

Supports two message formats:
1. JSON text messages (ping, status, shutdown)
2. Binary FRAME messages (5-byte magic + header + pixel data)

M3: Uses real CorridorKey MLX inference when the model is loaded.
Falls back to text overlay when the model is not available.
"""

import json
import logging
import struct
import time
from typing import Any, Optional

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from engines.base import InferenceEngine, InferenceRequest
from server.hardware import detect_hardware

logger = logging.getLogger("corridorkey.handler")

# Binary frame header:
#   "FRAME" (5) + width(4) + height(4) + rowbytes(4) + output_mode(1)
#   + despill(4f) + despeckle(4f) + refiner(4f) + matte_cleanup(4f)
#   + has_hint(1) + [hint_width(4) + hint_height(4) + hint_rowbytes(4)]
#   + pixel_data + [hint_pixel_data]
FRAME_MAGIC = b"FRAME"
FRAME_HEADER_BASE = 5 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 1  # 39 bytes
# Legacy header without params (M2 compat)
FRAME_HEADER_SIZE_LEGACY = 5 + 4 + 4 + 4  # 17 bytes


class RequestHandler:
    """Dispatches incoming messages from the AE plugin."""

    def __init__(self, engine: Optional[InferenceEngine] = None) -> None:
        self._hw_info = detect_hardware()
        self._frame_count = 0
        self._engine = engine
        self._last_inference_ms: float = 0

    def handle_raw(self, data: bytes) -> bytes:
        """Handle a raw message (bytes). Detect format and dispatch."""

        # Check for binary FRAME message
        if len(data) >= FRAME_HEADER_SIZE_LEGACY and data[:5] == FRAME_MAGIC:
            return self._handle_frame_binary(data)

        # Otherwise treat as JSON text
        try:
            msg = json.loads(data.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return b"ERROR" + b"Unknown message format"

        response = self._handle_json(msg)
        return json.dumps(response).encode("utf-8")

    def _handle_json(self, msg: dict[str, Any]) -> dict[str, Any]:
        """Route a JSON message to the appropriate handler."""
        msg_type = msg.get("type", "unknown")

        handlers = {
            "ping": self._handle_ping,
            "status": self._handle_status,
            "shutdown": self._handle_shutdown,
        }

        handler = handlers.get(msg_type)
        if handler is None:
            logger.warning("Unknown message type: %s", msg_type)
            return {"type": "error", "message": f"Unknown message type: {msg_type}"}

        return handler(msg)

    def _handle_ping(self, msg: dict[str, Any]) -> dict[str, Any]:
        return {"type": "pong", "version": "0.1.0"}

    def _handle_status(self, msg: dict[str, Any]) -> dict[str, Any]:
        engine_ready = self._engine is not None and self._engine.is_ready()
        return {
            "type": "status",
            "device": self._hw_info,
            "model_state": "ready" if engine_ready else "not_loaded",
            "warmup_complete": engine_ready,
            "frames_processed": self._frame_count,
            "last_inference_ms": self._last_inference_ms,
        }

    def _handle_shutdown(self, msg: dict[str, Any]) -> dict[str, Any]:
        logger.info("Shutdown requested by plugin")
        return {"type": "shutdown_ack"}

    def _handle_frame_binary(self, data: bytes) -> bytes:
        """Process a binary FRAME message through inference or fallback."""

        # Parse header — detect if it's the extended format (with params) or legacy
        width = struct.unpack(">I", data[5:9])[0]
        height = struct.unpack(">I", data[9:13])[0]
        rowbytes = struct.unpack(">I", data[13:17])[0]

        hint_data: Optional[bytes] = None
        hint_width = 0
        hint_height = 0
        hint_rowbytes = 0

        # Check if we have the extended header (byte 34 = has_hint flag)
        if len(data) >= FRAME_HEADER_BASE:
            # Extended format: has output_mode + param floats + hint flag
            output_mode = data[17]
            despill = struct.unpack(">f", data[18:22])[0]
            despeckle = struct.unpack(">f", data[22:26])[0]
            refiner = struct.unpack(">f", data[26:30])[0]
            matte_cleanup = struct.unpack(">f", data[30:34])[0]
            has_hint = data[34] != 0

            offset = 35
            if has_hint:
                hint_width = struct.unpack(">I", data[offset:offset+4])[0]
                hint_height = struct.unpack(">I", data[offset+4:offset+8])[0]
                hint_rowbytes = struct.unpack(">I", data[offset+8:offset+12])[0]
                offset += 12

            pixel_size = height * rowbytes
            pixel_data = data[offset:offset + pixel_size]
            offset += pixel_size

            if has_hint and hint_height > 0 and hint_rowbytes > 0:
                hint_size = hint_height * hint_rowbytes
                hint_data = data[offset:offset + hint_size]
                logger.info("Alpha hint received: %dx%d", hint_width, hint_height)
        else:
            # Legacy format (M2 compat): no params
            output_mode = 0
            despill = 0.5
            despeckle = 0.0
            refiner = 0.5
            matte_cleanup = 0.0
            pixel_data = data[FRAME_HEADER_SIZE_LEGACY:]

        logger.info("Frame %dx%d mode=%d (despill=%.2f refiner=%.2f hint=%s)",
                     width, height, output_mode, despill, refiner,
                     f"{hint_width}x{hint_height}" if hint_data else "none")

        self._frame_count += 1

        # --- Try real inference ---
        if self._engine is not None and self._engine.is_ready():
            return self._process_with_engine(
                width, height, rowbytes, pixel_data,
                output_mode, despill, despeckle, refiner, matte_cleanup,
                hint_data, hint_width, hint_height, hint_rowbytes,
            )

        # --- Fallback: text overlay ---
        return self._process_fallback_overlay(width, height, rowbytes, pixel_data)

    def _process_with_engine(
        self, width: int, height: int, rowbytes: int, pixel_data: bytes,
        output_mode: int, despill: float, despeckle: float,
        refiner: float, matte_cleanup: float,
        hint_data: Optional[bytes] = None,
        hint_width: int = 0, hint_height: int = 0, hint_rowbytes: int = 0,
    ) -> bytes:
        """Process a frame through the real inference engine."""
        try:
            # Convert raw pixel bytes to (H, W, 4) ARGB array
            raw = np.frombuffer(pixel_data, dtype=np.uint8).copy()
            if rowbytes == width * 4:
                argb = raw.reshape((height, width, 4))
            else:
                full = raw.reshape((height, rowbytes))
                argb = full[:, :width * 4].reshape((height, width, 4))

            # Parse alpha hint if provided
            alpha_hint_image: Optional[np.ndarray] = None
            if hint_data is not None and len(hint_data) > 0:
                hint_raw = np.frombuffer(hint_data, dtype=np.uint8).copy()
                if hint_rowbytes == hint_width * 4:
                    hint_argb = hint_raw.reshape((hint_height, hint_width, 4))
                else:
                    hint_full = hint_raw.reshape((hint_height, hint_rowbytes))
                    hint_argb = hint_full[:, :hint_width * 4].reshape((hint_height, hint_width, 4))
                # Convert ARGB hint to grayscale (use luminance of RGB channels)
                # The hint layer might be a B&W matte — average RGB
                hint_r = hint_argb[:, :, 1].astype(np.float32)
                hint_g = hint_argb[:, :, 2].astype(np.float32)
                hint_b = hint_argb[:, :, 3].astype(np.float32)
                alpha_hint_image = ((hint_r * 0.299 + hint_g * 0.587 + hint_b * 0.114)).astype(np.uint8)
                # Resize hint to match input if needed
                if alpha_hint_image.shape[:2] != (height, width):
                    from PIL import Image as _PILImg
                    alpha_hint_image = np.array(
                        _PILImg.fromarray(alpha_hint_image, "L").resize((width, height), _PILImg.Resampling.BILINEAR)
                    )

            # Build inference request
            request = InferenceRequest(
                image=argb,
                output_mode=output_mode,
                despill=despill,
                despeckle=despeckle,
                refiner=refiner,
                matte_cleanup=matte_cleanup,
            )
            # Attach the alpha hint to the request for the engine
            request._alpha_hint = alpha_hint_image  # type: ignore[attr-defined]

            # Run inference
            t0 = time.perf_counter()
            result = self._engine.process(request)
            elapsed_ms = (time.perf_counter() - t0) * 1000
            self._last_inference_ms = elapsed_ms

            if not result.success:
                logger.error("Inference failed: %s", result.error)
                return b"ERROR" + result.error.encode("utf-8")

            logger.info("Inference complete in %.1fms", elapsed_ms)

            # Convert output (H, W, 4) ARGB back to padded pixel data
            output_argb = result.image
            if rowbytes == width * 4:
                out_pixels = output_argb.tobytes()
            else:
                padded = np.zeros((height, rowbytes), dtype=np.uint8)
                padded[:, :width * 4] = output_argb.reshape((height, width * 4))
                out_pixels = padded.tobytes()

            # Build response
            response = bytearray(FRAME_HEADER_SIZE_LEGACY + len(out_pixels))
            response[:5] = FRAME_MAGIC
            struct.pack_into(">I", response, 5, width)
            struct.pack_into(">I", response, 9, height)
            struct.pack_into(">I", response, 13, rowbytes)
            response[FRAME_HEADER_SIZE_LEGACY:] = out_pixels
            return bytes(response)

        except Exception as e:
            logger.exception("Engine processing error")
            return b"ERROR" + str(e).encode("utf-8")

    def _process_fallback_overlay(
        self, width: int, height: int, rowbytes: int, pixel_data: bytes,
    ) -> bytes:
        """Fallback: stamp text overlay when engine isn't available."""
        try:
            raw = np.frombuffer(pixel_data, dtype=np.uint8).copy()
            if rowbytes == width * 4:
                img_array = raw.reshape((height, width, 4))
            else:
                full = raw.reshape((height, rowbytes))
                img_array = full[:, :width * 4].reshape((height, width, 4))

            # ARGB → RGBA for PIL
            rgba = np.zeros_like(img_array)
            rgba[:, :, 0] = img_array[:, :, 1]
            rgba[:, :, 1] = img_array[:, :, 2]
            rgba[:, :, 2] = img_array[:, :, 3]
            rgba[:, :, 3] = img_array[:, :, 0]

            img = Image.fromarray(rgba, "RGBA")
            draw = ImageDraw.Draw(img)

            bar_height = max(50, height // 10)
            overlay = Image.new("RGBA", (width, bar_height), (0, 80, 0, 200))
            img.paste(overlay, (0, 0), overlay)

            draw = ImageDraw.Draw(img)
            try:
                font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", size=max(20, height // 20))
            except (IOError, OSError):
                font = ImageFont.load_default()

            draw.text((10, 5), "CORRIDORKEY IPC OK  (NO MODEL)", fill=(255, 200, 0, 255), font=font)
            draw.text((10, bar_height // 2 + 2),
                      f"Frame #{self._frame_count}  |  Model not loaded",
                      fill=(200, 255, 200, 255), font=font)

            # RGBA → ARGB
            result = np.array(img)
            argb = np.zeros_like(result)
            argb[:, :, 0] = result[:, :, 3]
            argb[:, :, 1] = result[:, :, 0]
            argb[:, :, 2] = result[:, :, 1]
            argb[:, :, 3] = result[:, :, 2]

            if rowbytes == width * 4:
                out_pixels = argb.tobytes()
            else:
                padded = np.zeros((height, rowbytes), dtype=np.uint8)
                padded[:, :width * 4] = argb.reshape((height, width * 4))
                out_pixels = padded.tobytes()

            response = bytearray(FRAME_HEADER_SIZE_LEGACY + len(out_pixels))
            response[:5] = FRAME_MAGIC
            struct.pack_into(">I", response, 5, width)
            struct.pack_into(">I", response, 9, height)
            struct.pack_into(">I", response, 13, rowbytes)
            response[FRAME_HEADER_SIZE_LEGACY:] = out_pixels
            return bytes(response)

        except Exception as e:
            logger.exception("Fallback overlay error")
            return b"ERROR" + str(e).encode("utf-8")

"""
Request handler — dispatches incoming IPC messages to the appropriate action.

Supports two message formats:
1. JSON text messages (ping, status, shutdown)
2. Binary FRAME messages (5-byte magic + header + pixel data)
"""

import logging
import struct
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from server.hardware import detect_hardware

logger = logging.getLogger("corridorkey.handler")

# Binary frame header: "FRAME" + width(4) + height(4) + rowbytes(4)
FRAME_MAGIC = b"FRAME"
FRAME_HEADER_SIZE = 5 + 4 + 4 + 4


class RequestHandler:
    """Dispatches incoming messages from the AE plugin."""

    def __init__(self) -> None:
        self._hw_info = detect_hardware()
        self._frame_count = 0

    def handle_raw(self, data: bytes) -> bytes:
        """Handle a raw message (bytes). Detect format and dispatch."""

        # Check for binary FRAME message
        if data[:5] == FRAME_MAGIC and len(data) >= FRAME_HEADER_SIZE:
            return self._handle_frame_binary(data)

        # Otherwise treat as JSON text
        import json
        try:
            msg = json.loads(data.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            error = b"ERROR" + b"Unknown message format"
            return error

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
        return {
            "type": "status",
            "device": self._hw_info,
            "model_state": "mock",
            "warmup_complete": True,
            "frames_processed": self._frame_count,
        }

    def _handle_shutdown(self, msg: dict[str, Any]) -> dict[str, Any]:
        logger.info("Shutdown requested by plugin")
        return {"type": "shutdown_ack"}

    def _handle_frame_binary(self, data: bytes) -> bytes:
        """Process a binary FRAME message: stamp text overlay and return."""
        # Parse header
        width = struct.unpack(">I", data[5:9])[0]
        height = struct.unpack(">I", data[9:13])[0]
        rowbytes = struct.unpack(">I", data[13:17])[0]
        pixel_data = data[FRAME_HEADER_SIZE:]

        logger.info("Processing frame: %dx%d (rowbytes=%d, data=%d bytes)",
                     width, height, rowbytes, len(pixel_data))

        self._frame_count += 1

        # Convert ARGB pixel data to numpy array
        # AE sends ARGB 8bpc, row-padded to rowbytes
        try:
            img_array = np.frombuffer(pixel_data, dtype=np.uint8).copy()

            # Handle row padding: extract width*4 bytes per row from rowbytes-strided data
            if rowbytes == width * 4:
                img_array = img_array.reshape((height, width, 4))
            else:
                # Skip padding bytes per row
                full = img_array.reshape((height, rowbytes))
                img_array = full[:, :width * 4].reshape((height, width, 4))

            # ARGB → RGBA for PIL
            rgba = np.zeros_like(img_array)
            rgba[:, :, 0] = img_array[:, :, 1]  # R
            rgba[:, :, 1] = img_array[:, :, 2]  # G
            rgba[:, :, 2] = img_array[:, :, 3]  # B
            rgba[:, :, 3] = img_array[:, :, 0]  # A

            # Create PIL image and draw text
            img = Image.fromarray(rgba, "RGBA")
            draw = ImageDraw.Draw(img)

            # Draw semi-transparent green bar at top
            bar_height = max(50, height // 10)
            overlay = Image.new("RGBA", (width, bar_height), (0, 80, 0, 200))
            img.paste(overlay, (0, 0), overlay)

            # Draw text
            draw = ImageDraw.Draw(img)
            try:
                font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", size=max(20, height // 20))
            except (IOError, OSError):
                font = ImageFont.load_default()

            draw.text((10, 5), "CORRIDORKEY IPC OK", fill=(0, 255, 100, 255), font=font)
            draw.text((10, bar_height // 2 + 2),
                      f"Frame #{self._frame_count}  |  Python Runtime  |  {width}x{height}",
                      fill=(200, 255, 200, 255), font=font)

            # RGBA → ARGB for AE
            result = np.array(img)
            argb = np.zeros_like(result)
            argb[:, :, 0] = result[:, :, 3]  # A
            argb[:, :, 1] = result[:, :, 0]  # R
            argb[:, :, 2] = result[:, :, 1]  # G
            argb[:, :, 3] = result[:, :, 2]  # B

            # Rebuild with original rowbytes padding
            if rowbytes == width * 4:
                out_pixels = argb.tobytes()
            else:
                padded = np.zeros((height, rowbytes), dtype=np.uint8)
                padded[:, :width * 4] = argb.reshape((height, width * 4))
                out_pixels = padded.tobytes()

        except Exception as e:
            logger.exception("Frame processing error")
            return b"ERROR" + str(e).encode("utf-8")

        # Build response: FRAME header + pixel data
        response = bytearray(FRAME_HEADER_SIZE + len(out_pixels))
        response[:5] = FRAME_MAGIC
        struct.pack_into(">I", response, 5, width)
        struct.pack_into(">I", response, 9, height)
        struct.pack_into(">I", response, 13, rowbytes)
        response[FRAME_HEADER_SIZE:] = out_pixels

        return bytes(response)

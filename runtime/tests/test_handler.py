"""Tests for the request handler."""

import json
import struct

import numpy as np

from server.handler import RequestHandler


def test_ping():
    handler = RequestHandler()
    resp = handler.handle_raw(b'{"type": "ping"}')
    data = json.loads(resp)
    assert data["type"] == "pong"
    assert "version" in data


def test_status():
    handler = RequestHandler()
    resp = handler.handle_raw(b'{"type": "status"}')
    data = json.loads(resp)
    assert data["type"] == "status"
    assert "device" in data
    assert "model_state" in data


def test_unknown_type():
    handler = RequestHandler()
    resp = handler.handle_raw(b'{"type": "nonsense"}')
    data = json.loads(resp)
    assert data["type"] == "error"


def _build_frame_msg(width, height, pixels, output_mode=0, has_hint=False,
                     hint_pixels=None, hint_width=0, hint_height=0):
    """Helper to build a FRAME message in the extended format."""
    rowbytes = width * 4
    msg = bytearray()
    msg += b"FRAME"
    msg += struct.pack(">I", width)
    msg += struct.pack(">I", height)
    msg += struct.pack(">I", rowbytes)
    msg += struct.pack("B", output_mode)        # output_mode
    msg += struct.pack(">f", 0.5)               # despill
    msg += struct.pack(">f", 0.0)               # despeckle
    msg += struct.pack(">f", 0.5)               # refiner
    msg += struct.pack(">f", 0.0)               # matte_cleanup
    msg += struct.pack("B", 1 if has_hint else 0)  # has_hint
    if has_hint:
        hint_rowbytes = hint_width * 4
        msg += struct.pack(">I", hint_width)
        msg += struct.pack(">I", hint_height)
        msg += struct.pack(">I", hint_rowbytes)
    msg += pixels.tobytes()
    if has_hint and hint_pixels is not None:
        msg += hint_pixels.tobytes()
    return bytes(msg)


def test_process_frame_returns_modified_pixels():
    """Send a small test frame and verify it comes back as FRAME with same dimensions."""
    handler = RequestHandler()

    width, height = 64, 48

    # Create a solid red frame (ARGB format)
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255  # A
    pixels[:, :, 1] = 255  # R
    pixels[:, :, 2] = 0    # G
    pixels[:, :, 3] = 0    # B

    msg = _build_frame_msg(width, height, pixels)

    resp = handler.handle_raw(bytes(msg))

    # Response should be a FRAME
    assert resp[:5] == b"FRAME", f"Expected FRAME header, got {resp[:5]}"
    resp_w = struct.unpack(">I", resp[5:9])[0]
    resp_h = struct.unpack(">I", resp[9:13])[0]
    resp_rb = struct.unpack(">I", resp[13:17])[0]
    assert resp_w == width
    assert resp_h == height
    assert resp_rb == width * 4

    # Pixel data should be present and same size
    resp_pixels = resp[17:]
    expected_size = width * height * 4
    assert len(resp_pixels) == expected_size

    # Pixels should be different (text was stamped on them)
    assert resp_pixels != pixels.tobytes(), "Pixels should be modified by the handler"


def test_frame_count_increments():
    """Verify the frame counter increments with each processed frame."""
    handler = RequestHandler()

    width, height = 32, 32
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255  # A

    msg = _build_frame_msg(width, height, pixels)

    # Process two frames
    handler.handle_raw(msg)
    handler.handle_raw(msg)

    # Check status
    resp = handler.handle_raw(b'{"type": "status"}')
    data = json.loads(resp)
    assert data["frames_processed"] == 2

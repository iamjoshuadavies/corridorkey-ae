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
    msg += struct.pack("B", 3)                      # quality_mode (3=tiled)
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


def test_cache_hit_on_identical_frame():
    """Sending the same frame twice should return cached result on second call."""
    handler = RequestHandler()

    width, height = 64, 48
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255  # A
    pixels[:, :, 1] = 128  # R

    msg = _build_frame_msg(width, height, pixels)

    # First call — cache miss
    resp1 = handler.handle_raw(msg)
    assert resp1[:5] == b"FRAME"

    # Second call — should be cache hit (identical frame + params)
    resp2 = handler.handle_raw(msg)
    assert resp2[:5] == b"FRAME"
    assert resp1 == resp2  # Exact same response

    # Check cache stats
    status = json.loads(handler.handle_raw(b'{"type": "status"}'))
    assert status["cache"]["hits"] >= 1


def test_cache_miss_on_different_params():
    """Changing params should cause a cache miss."""
    handler = RequestHandler()

    width, height = 32, 32
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255

    msg1 = _build_frame_msg(width, height, pixels, output_mode=0)
    msg2 = _build_frame_msg(width, height, pixels, output_mode=1)

    handler.handle_raw(msg1)
    handler.handle_raw(msg2)

    status = json.loads(handler.handle_raw(b'{"type": "status"}'))
    assert status["cache"]["misses"] >= 2  # Both were misses


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


# ---------------------------------------------------------------------------
# Regression tests for cache-hash bugs fixed during the Windows port
# ---------------------------------------------------------------------------


def test_cache_miss_when_hint_edited_mid_frame():
    """Regression for the corner-only pixel hash bug.

    Previously `FrameCache.hash_pixels` sampled only first 16 KB + last
    16 KB of the buffer, so changes in the middle of the hint mask
    (the subject area, where keying matters) never flipped the hash
    and the cache returned stale output. Full-buffer hashing fixes it.
    """
    handler = RequestHandler()

    # 1080p-shaped mini frame — big enough that the corner-sampling bug
    # would have been triggered by the old hash implementation.
    width, height = 256, 256
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255  # A
    pixels[:, :, 1] = 128  # R

    # Hint A: all-white in the middle, all-black at the edges
    hint_a = np.zeros((height, width, 4), dtype=np.uint8)
    hint_a[:, :, 0] = 255  # A
    hint_a[height // 4:3 * height // 4, width // 4:3 * width // 4, 1:] = 255  # white center

    # Hint B: same edges, DIFFERENT center fill — should flip the hash.
    hint_b = hint_a.copy()
    hint_b[height // 4:3 * height // 4, width // 4:3 * width // 4, 1:] = 128  # gray center

    msg_a = _build_frame_msg(
        width, height, pixels,
        has_hint=True, hint_pixels=hint_a, hint_width=width, hint_height=height,
    )
    msg_b = _build_frame_msg(
        width, height, pixels,
        has_hint=True, hint_pixels=hint_b, hint_width=width, hint_height=height,
    )

    # Both should be cache misses — different hint content means different key.
    handler.handle_raw(msg_a)
    handler.handle_raw(msg_b)
    status = json.loads(handler.handle_raw(b'{"type": "status"}'))
    assert status["cache"]["misses"] >= 2, (
        "Both frames should miss the cache — hint content differs"
    )


def test_cache_miss_when_quality_mode_changes():
    """Changing the Quality dropdown must invalidate the response cache.

    Regression for a phase during the Windows port where quality_mode
    wasn't part of the handler's frame cache key, so switching from
    Full Res to Fastest on the same frame returned stale output.
    """
    handler = RequestHandler()

    width, height = 32, 32
    pixels = np.zeros((height, width, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255

    # Same frame, different quality — should miss on both.
    def _msg_with_quality(quality: int) -> bytes:
        rowbytes = width * 4
        msg = bytearray()
        msg += b"FRAME"
        msg += struct.pack(">I", width)
        msg += struct.pack(">I", height)
        msg += struct.pack(">I", rowbytes)
        msg += struct.pack("B", 0)              # output_mode
        msg += struct.pack(">f", 0.5)           # despill
        msg += struct.pack(">f", 0.0)           # despeckle
        msg += struct.pack(">f", 0.5)           # refiner
        msg += struct.pack(">f", 0.0)           # matte_cleanup
        msg += struct.pack("B", quality)        # quality_mode
        msg += struct.pack("B", 0)              # has_hint
        msg += pixels.tobytes()
        return bytes(msg)

    handler.handle_raw(_msg_with_quality(0))    # Fastest
    handler.handle_raw(_msg_with_quality(3))    # Full Res (Tiled)

    status = json.loads(handler.handle_raw(b'{"type": "status"}'))
    assert status["cache"]["misses"] >= 2, (
        "Both quality modes should miss — quality_mode must be in the cache key"
    )

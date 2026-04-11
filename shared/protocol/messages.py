"""
Shared IPC message definitions.

Both the C++ plugin (via equivalent struct definitions) and the Python
runtime use this schema. Changes here must be reflected in both sides.

Protocol: msgpack-encoded dicts over length-prefixed TCP/Unix sockets.
"""

from typing import Any, TypedDict

# --- Message Types ---
MSG_PING = "ping"
MSG_PONG = "pong"
MSG_STATUS = "status"
MSG_STATUS_RESPONSE = "status_response"
MSG_PROCESS_FRAME = "process_frame"
MSG_FRAME_RESULT = "frame_result"
MSG_SHUTDOWN = "shutdown"
MSG_SHUTDOWN_ACK = "shutdown_ack"
MSG_ERROR = "error"


# --- Type definitions for documentation ---

class PingMessage(TypedDict):
    type: str  # "ping"


class PongMessage(TypedDict):
    type: str  # "pong"
    version: str


class StatusRequest(TypedDict):
    type: str  # "status"


class StatusResponse(TypedDict):
    type: str  # "status"
    device: dict[str, Any]
    model_state: str
    warmup_complete: bool


class ProcessFrameMessage(TypedDict):
    type: str  # "process_frame"
    width: int
    height: int
    rowbytes: int
    pixel_data: bytes  # Raw ARGB 8bpc
    output_mode: int
    device_mode: int
    quality_mode: int
    low_memory: bool
    tile_size: int
    despill: float
    despeckle: float
    refiner: float
    matte_cleanup: float


class FrameResultMessage(TypedDict):
    type: str  # "frame_result"
    success: bool
    width: int
    height: int
    rowbytes: int
    pixel_data: bytes
    error: str


# --- Protocol constants ---
PROTOCOL_VERSION = 1
HEADER_SIZE = 4  # 4-byte big-endian length prefix
MAX_MESSAGE_SIZE = 256 * 1024 * 1024  # 256 MB

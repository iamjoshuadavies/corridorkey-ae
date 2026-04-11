"""
Request handler — dispatches incoming IPC messages to the appropriate action.
"""

import logging
from typing import Any

from server.hardware import detect_hardware

logger = logging.getLogger("corridorkey.handler")


class RequestHandler:
    """Dispatches incoming messages from the AE plugin."""

    def __init__(self) -> None:
        self._hw_info = detect_hardware()

    def handle(self, msg: dict[str, Any]) -> dict[str, Any]:
        """Route a message to the appropriate handler and return a response."""
        msg_type = msg.get("type", "unknown")

        handlers = {
            "ping": self._handle_ping,
            "status": self._handle_status,
            "process_frame": self._handle_process_frame,
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
            "model_state": "not_loaded",  # M3: actual model state
            "warmup_complete": False,
        }

    def _handle_process_frame(self, msg: dict[str, Any]) -> dict[str, Any]:
        # M3: Actual inference. For now, return an error.
        return {
            "type": "frame_result",
            "success": False,
            "error": "Inference not yet implemented (M1 shell)",
        }

    def _handle_shutdown(self, msg: dict[str, Any]) -> dict[str, Any]:
        logger.info("Shutdown requested by plugin")
        return {"type": "shutdown_ack"}

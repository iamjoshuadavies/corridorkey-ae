"""
IPC Server — Local socket server for plugin-runtime communication.

Protocol: Length-prefixed messages over TCP or Unix domain sockets.
Each message is prefixed with a 4-byte big-endian length header.

Messages can be:
  - JSON text (for control: ping, status, shutdown)
  - Binary FRAME data (for frame processing)

The handler.handle_raw() method accepts raw bytes and dispatches accordingly.
"""

import logging
import socket
import struct
from pathlib import Path
from typing import Optional

from server.handler import RequestHandler

logger = logging.getLogger("corridorkey.ipc")

# 4-byte big-endian length prefix
HEADER_FORMAT = ">I"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_MESSAGE_SIZE = 256 * 1024 * 1024  # 256 MB (large frames)


class IPCServer:
    """Local IPC server for the AE plugin bridge."""

    def __init__(self, port: int = 0, socket_path: Optional[str] = None) -> None:
        self.port = port
        self.socket_path = socket_path
        self._running = False
        self._server_socket: Optional[socket.socket] = None
        self._handler = RequestHandler()

    def run(self) -> None:
        """Start the server and accept connections."""
        if self.socket_path:
            self._server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            path = Path(self.socket_path)
            path.unlink(missing_ok=True)
            self._server_socket.bind(str(path))
            logger.info("Listening on Unix socket: %s", self.socket_path)
        else:
            self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server_socket.bind(("127.0.0.1", self.port))
            actual_port = self._server_socket.getsockname()[1]
            logger.info("Listening on TCP port: %d", actual_port)
            # Write the port so the plugin can find us
            print(f"PORT:{actual_port}", flush=True)

        self._server_socket.listen(1)
        self._running = True

        while self._running:
            try:
                self._server_socket.settimeout(1.0)
                try:
                    conn, addr = self._server_socket.accept()
                except socket.timeout:
                    continue
                logger.info("Client connected: %s", addr)
                self._handle_connection(conn)
            except OSError:
                if self._running:
                    raise
                break

    def stop(self) -> None:
        """Shut down the server."""
        self._running = False
        if self._server_socket:
            self._server_socket.close()
            self._server_socket = None
        if self.socket_path:
            Path(self.socket_path).unlink(missing_ok=True)

    def _handle_connection(self, conn: socket.socket) -> None:
        """Handle a single client connection (one plugin instance)."""
        with conn:
            conn.settimeout(30.0)
            while self._running:
                try:
                    data = self._recv_raw(conn)
                    if data is None:
                        break
                    response = self._handler.handle_raw(data)
                    self._send_raw(conn, response)
                except (ConnectionResetError, BrokenPipeError):
                    logger.info("Client disconnected")
                    break
                except socket.timeout:
                    continue
                except Exception:
                    logger.exception("Error handling message")
                    break

    def _recv_raw(self, conn: socket.socket) -> Optional[bytes]:
        """Receive a length-prefixed raw message."""
        header = self._recv_exact(conn, HEADER_SIZE)
        if not header:
            return None
        (length,) = struct.unpack(HEADER_FORMAT, header)
        if length > MAX_MESSAGE_SIZE:
            raise ValueError(f"Message too large: {length}")
        data = self._recv_exact(conn, length)
        return data

    def _send_raw(self, conn: socket.socket, data: bytes) -> None:
        """Send a length-prefixed raw message."""
        header = struct.pack(HEADER_FORMAT, len(data))
        conn.sendall(header + data)

    @staticmethod
    def _recv_exact(conn: socket.socket, size: int) -> Optional[bytes]:
        """Receive exactly `size` bytes from the socket."""
        chunks: list[bytes] = []
        received = 0
        while received < size:
            chunk = conn.recv(min(size - received, 65536))
            if not chunk:
                return None
            chunks.append(chunk)
            received += len(chunk)
        return b"".join(chunks)

"""
CorridorKey AE Runtime — Main entry point.

Launches the inference server that communicates with the AE plugin
over a local socket using msgpack-framed messages.
"""

import argparse
import logging
import signal
import sys

from server.ipc import IPCServer
from server.hardware import detect_hardware

logger = logging.getLogger("corridorkey.runtime")


def main() -> None:
    parser = argparse.ArgumentParser(description="CorridorKey AE inference runtime")
    parser.add_argument("--port", type=int, default=0, help="TCP port (0 = auto-assign)")
    parser.add_argument("--socket", type=str, default=None, help="Unix domain socket path")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    # Detect hardware on startup
    hw_info = detect_hardware()
    logger.info("Hardware: %s", hw_info)

    # Create and start IPC server
    server = IPCServer(port=args.port, socket_path=args.socket)

    # Graceful shutdown on signals
    def handle_signal(sig: int, frame: object) -> None:
        logger.info("Received signal %d, shutting down...", sig)
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    logger.info("Starting CorridorKey runtime server...")
    server.run()


if __name__ == "__main__":
    main()

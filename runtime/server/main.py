"""
CorridorKey AE Runtime — Main entry point.

Launches the inference server that communicates with the AE plugin
over a local socket using length-prefixed binary messages.
"""

import argparse
import logging
import signal
import sys
from typing import Optional

from engines.base import InferenceEngine
from server.ipc import IPCServer
from server.hardware import detect_hardware

logger = logging.getLogger("corridorkey.runtime")


def create_engine(model_path: Optional[str] = None, tile_size: int = 512) -> Optional[InferenceEngine]:
    """Create and load the best available inference engine."""

    # Try MLX first (Apple Silicon)
    try:
        from engines.mlx_engine import MLXEngine, find_model_weights

        # Find model weights
        if model_path is None:
            weights = find_model_weights()
            if weights is None:
                logger.warning("No MLX model weights found — running without inference")
                return None
            model_path = str(weights)

        engine = MLXEngine(tile_size=tile_size, use_refiner=True)
        engine.load_model(model_path)
        logger.info("MLX engine ready: %s", engine.device_name)
        return engine

    except ImportError:
        logger.info("corridorkey_mlx not available, skipping MLX engine")
    except Exception:
        logger.exception("Failed to initialize MLX engine")

    # TODO: Try PyTorch engine (CUDA/MPS)

    logger.warning("No inference engine available — running in mock mode")
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description="CorridorKey AE inference runtime")
    parser.add_argument("--port", type=int, default=0, help="TCP port (0 = auto-assign)")
    parser.add_argument("--socket", type=str, default=None, help="Unix domain socket path")
    parser.add_argument("--model", type=str, default=None, help="Path to model weights")
    parser.add_argument("--tile-size", type=int, default=512, help="Tile size for tiled inference (512=default, larger=more VRAM)")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    # Detect hardware on startup
    hw_info = detect_hardware()
    logger.info("Hardware: %s", hw_info)

    # Initialize inference engine
    engine = create_engine(model_path=args.model, tile_size=args.tile_size)

    # Create and start IPC server with engine
    from server.handler import RequestHandler
    handler = RequestHandler(engine=engine)

    server = IPCServer(port=args.port, socket_path=args.socket)
    server._handler = handler  # Inject the handler with engine

    # Graceful shutdown on signals
    def handle_signal(sig: int, frame: object) -> None:
        logger.info("Received signal %d, shutting down...", sig)
        if engine:
            engine.unload()
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    logger.info("Starting CorridorKey runtime server...")
    server.run()


if __name__ == "__main__":
    main()

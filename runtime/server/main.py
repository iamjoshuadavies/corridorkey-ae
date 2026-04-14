"""
CorridorKey AE Runtime — Main entry point.

Launches the inference server that communicates with the AE plugin
over a local socket using length-prefixed binary messages.
"""

import argparse
import logging
import signal
import sys
import tempfile
import threading
from pathlib import Path

from engines.base import InferenceEngine
from server.hardware import detect_hardware
from server.ipc import IPCServer

logger = logging.getLogger("corridorkey.runtime")


def create_engine(model_path: str | None = None) -> InferenceEngine | None:
    """Create and load the best available inference engine.

    Order of preference:
      1. MLX (Apple Silicon, fast on M-series with unified memory)
      2. PyTorch (CUDA on Windows/Linux with NVIDIA GPU; CPU fallback)
    """

    engine: InferenceEngine | None = None

    # --- 1. MLX (Apple Silicon) ---
    try:
        from engines.mlx_engine import MLXEngine, find_model_weights

        if model_path is None:
            weights = find_model_weights()
            if weights is not None:
                mlx = MLXEngine(use_refiner=True)
                mlx.load_model(str(weights))
                logger.info("MLX engine ready: %s", mlx.device_name)
                return mlx
            logger.info("No MLX model weights found — falling through to PyTorch")
        else:
            mlx = MLXEngine(use_refiner=True)
            mlx.load_model(model_path)
            logger.info("MLX engine ready: %s", mlx.device_name)
            return mlx
    except ImportError:
        logger.info("corridorkey_mlx not available, skipping MLX engine")
    except Exception:
        logger.exception("Failed to initialize MLX engine")

    # --- 2. PyTorch (CUDA on Windows/Linux, CPU fallback) ---
    # Don't gate on a precheck — let load_model("") handle discovery and
    # downloading internally. The first run on a fresh install pulls the
    # ~398 MB weights from the corridorkey-mlx GitHub release.
    try:
        from engines.pytorch_engine import PyTorchEngine

        pt = PyTorchEngine(prefer_fp16=True)
        pt.load_model(model_path or "")
        logger.info("PyTorch engine ready: %s", pt.device_name)
        return pt
    except ImportError as e:
        logger.info("PyTorch engine unavailable: %s", e)
    except FileNotFoundError as e:
        logger.warning("PyTorch engine not started: %s", e)
    except Exception:
        logger.exception("Failed to initialize PyTorch engine")

    logger.warning("No inference engine available — running in mock mode")
    return engine


def main() -> None:
    parser = argparse.ArgumentParser(description="CorridorKey AE inference runtime")
    parser.add_argument("--port", type=int, default=0, help="TCP port (0 = auto-assign)")
    parser.add_argument("--socket", type=str, default=None, help="Unix domain socket path")
    parser.add_argument("--model", type=str, default=None, help="Path to model weights")
    parser.add_argument(
        "--log-level", default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    args = parser.parse_args()

    # Log to both stderr (visible when run by hand) and a temp-file
    # (only way to see logs when the bridge launches us with no console).
    log_path = Path(tempfile.gettempdir()) / "corridorkey_runtime.log"
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        handlers=[
            logging.FileHandler(str(log_path), mode="w", encoding="utf-8"),
            logging.StreamHandler(sys.stderr),
        ],
    )
    logging.getLogger("corridorkey").info("Logging to %s", log_path)

    # Detect hardware on startup
    hw_info = detect_hardware()
    logger.info("Hardware: %s", hw_info)

    # Create the handler in "loading" state. The IPC server binds and starts
    # accepting connections IMMEDIATELY so the AE plugin can connect and see
    # a meaningful status while the engine is still loading (which can take
    # ~10 s on a fresh install due to the weight download).
    from server.handler import RequestHandler
    handler = RequestHandler(engine=None)
    handler.set_engine_state("loading", "Starting up")

    server = IPCServer(port=args.port, socket_path=args.socket)
    server._handler = handler  # Inject the handler

    # Load the inference engine in a background thread. The handler will
    # respond to FRAME messages with a LOADING error until engine_state is
    # "ready".
    _engine_slot: dict[str, InferenceEngine | None] = {"engine": None}

    def _load_engine_bg() -> None:
        try:
            handler.set_engine_state("loading", "Loading engine")
            eng = create_engine(model_path=args.model)
            _engine_slot["engine"] = eng
            if eng is not None:
                handler.attach_engine(eng)
                logger.info("Engine load complete — runtime ready")
            else:
                handler.set_engine_state("error", "No engine available")
        except Exception as e:
            logger.exception("Engine load thread crashed")
            handler.set_engine_state("error", str(e))

    engine_thread = threading.Thread(target=_load_engine_bg, daemon=True, name="ck-engine-load")
    engine_thread.start()

    # Graceful shutdown on signals
    def handle_signal(sig: int, frame: object) -> None:
        logger.info("Received signal %d, shutting down...", sig)
        eng = _engine_slot["engine"]
        if eng is not None:
            eng.unload()
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    logger.info("Starting CorridorKey runtime server...")
    server.run()


if __name__ == "__main__":
    main()

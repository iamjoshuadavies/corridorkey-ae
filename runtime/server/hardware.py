"""
Hardware detection — identifies GPU, VRAM, and compute capabilities.
"""

import logging
import platform
import subprocess
from dataclasses import dataclass, asdict
from typing import Any

logger = logging.getLogger("corridorkey.hardware")


@dataclass
class HardwareInfo:
    device_name: str = "CPU"
    device_type: str = "cpu"  # cpu, cuda, mps, mlx
    vram_total_mb: int = 0
    vram_used_mb: int = 0
    is_unified_memory: bool = False
    compute_capability: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def __str__(self) -> str:
        mem = f"{self.vram_total_mb}MB"
        if self.is_unified_memory:
            mem += " (unified)"
        return f"{self.device_name} [{self.device_type}] {mem}"


def detect_hardware() -> dict[str, Any]:
    """Detect available compute hardware. Returns best available device info."""
    info = HardwareInfo()

    # Try CUDA first
    try:
        import torch

        if torch.cuda.is_available():
            info.device_name = torch.cuda.get_device_name(0)
            info.device_type = "cuda"
            info.vram_total_mb = int(torch.cuda.get_device_properties(0).total_memory / (1024 * 1024))
            info.vram_used_mb = int(torch.cuda.memory_allocated(0) / (1024 * 1024))
            info.compute_capability = (
                f"{torch.cuda.get_device_properties(0).major}"
                f".{torch.cuda.get_device_properties(0).minor}"
            )
            logger.info("CUDA device detected: %s", info)
            return info.to_dict()

        if torch.backends.mps.is_available():
            info.device_name = _get_apple_gpu_name()
            info.device_type = "mps"
            info.is_unified_memory = True
            info.vram_total_mb = _get_apple_memory_mb()
            logger.info("Apple MPS device detected: %s", info)
            return info.to_dict()

    except ImportError:
        logger.debug("PyTorch not available, checking MLX...")

    # Try MLX (Apple Silicon)
    try:
        import mlx.core as mx

        info.device_name = _get_apple_gpu_name()
        info.device_type = "mlx"
        info.is_unified_memory = True
        info.vram_total_mb = _get_apple_memory_mb()
        logger.info("MLX device detected: %s", info)
        return info.to_dict()

    except ImportError:
        pass

    # Fallback: CPU only
    info.device_name = platform.processor() or "CPU"
    info.device_type = "cpu"
    logger.info("CPU-only mode: %s", info)
    return info.to_dict()


def _get_apple_gpu_name() -> str:
    """Get Apple Silicon GPU name via system_profiler."""
    try:
        result = subprocess.run(
            ["sysctl", "-n", "machdep.cpu.brand_string"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        return result.stdout.strip() or "Apple Silicon"
    except Exception:
        return "Apple Silicon"


def _get_apple_memory_mb() -> int:
    """Get total system memory on macOS (unified memory)."""
    try:
        import os

        total_bytes = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
        return int(total_bytes / (1024 * 1024))
    except Exception:
        return 0

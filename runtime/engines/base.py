"""
Base inference engine interface.

All engines (PyTorch/CUDA, PyTorch/MPS, MLX) implement this interface.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray


@dataclass
class InferenceRequest:
    """A frame to process."""
    image: NDArray[np.uint8]        # (H, W, 4) ARGB uint8
    output_mode: int = 0            # 0=processed, 1=matte, 2=foreground, 3=composite
    low_memory: bool = False
    tile_size: int = 512
    despill: float = 0.5
    despeckle: float = 0.0
    refiner: float = 0.5
    matte_cleanup: float = 0.0
    brightness: float = 1.0
    float_output: bool = False      # Future: return float32 instead of uint8


@dataclass
class InferenceResult:
    """Result of processing a frame."""
    image: NDArray[np.uint8]        # (H, W, 4) ARGB uint8 (float32 support planned)
    success: bool = True
    error: str = ""


class InferenceEngine(ABC):
    """Abstract base for all inference backends."""

    @abstractmethod
    def load_model(self, model_path: str) -> None:
        """Load model weights from disk."""
        ...

    @abstractmethod
    def is_ready(self) -> bool:
        """Check if the engine is loaded and ready for inference."""
        ...

    @abstractmethod
    def process(self, request: InferenceRequest) -> InferenceResult:
        """Process a single frame and return the result."""
        ...

    @abstractmethod
    def unload(self) -> None:
        """Unload model and free resources."""
        ...

    @property
    @abstractmethod
    def device_name(self) -> str:
        """Human-readable device name."""
        ...

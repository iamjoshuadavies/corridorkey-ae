"""
Model manager — handles model downloads, verification, and caching.

Models are stored in a platform-appropriate cache directory:
  - macOS: ~/Library/Application Support/CorridorKey/models/
  - Windows: %APPDATA%/CorridorKey/models/
  - Linux: ~/.local/share/corridorkey/models/
"""

import hashlib
import logging
import platform
from pathlib import Path

logger = logging.getLogger("corridorkey.models")


def get_models_dir() -> Path:
    """Get the platform-appropriate models directory."""
    system = platform.system()
    if system == "Darwin":
        base = Path.home() / "Library" / "Application Support" / "CorridorKey"
    elif system == "Windows":
        appdata = Path.home() / "AppData" / "Roaming"
        base = appdata / "CorridorKey"
    else:
        base = Path.home() / ".local" / "share" / "corridorkey"

    models_dir = base / "models"
    models_dir.mkdir(parents=True, exist_ok=True)
    return models_dir


class ModelManager:
    """Manages model weights: discovery, download, verification."""

    def __init__(self, models_dir: Path | None = None) -> None:
        self.models_dir = models_dir or get_models_dir()

    def is_model_available(self, model_name: str) -> bool:
        """Check if a model's weights exist locally."""
        model_path = self.models_dir / model_name
        return model_path.exists()

    def get_model_path(self, model_name: str) -> Path:
        """Get the local path for a model."""
        return self.models_dir / model_name

    def verify_model(self, model_name: str, expected_sha256: str) -> bool:
        """Verify a model file's SHA256 checksum."""
        model_path = self.models_dir / model_name
        if not model_path.exists():
            return False

        sha256 = hashlib.sha256()
        with open(model_path, "rb") as f:
            for chunk in iter(lambda: f.read(8192), b""):
                sha256.update(chunk)

        actual = sha256.hexdigest()
        if actual != expected_sha256:
            logger.error(
                "Checksum mismatch for %s: expected %s, got %s",
                model_name,
                expected_sha256[:16],
                actual[:16],
            )
            return False

        return True

    def list_models(self) -> list[str]:
        """List all model files in the models directory."""
        if not self.models_dir.exists():
            return []
        return [f.name for f in self.models_dir.iterdir() if f.is_file()]

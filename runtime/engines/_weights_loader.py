"""
Weights discovery + loading, shared by both inference engines.

Both the macOS MLX engine and the Windows PyTorch engine consume the
same `corridorkey_mlx.safetensors` release from upstream. This module
is the single place that knows how to:

  - Pick a platform-appropriate cache directory
      macOS  :  ~/Library/Application Support/CorridorKey/models/
      Windows:  %LOCALAPPDATA%/CorridorKey/models/
      Linux  :  $XDG_DATA_HOME/CorridorKey/models/
  - Download the official release on first run with progress logging
  - (PyTorch only) reverse-convert the MLX safetensors into a PyTorch
    state_dict, the inverse of corridorkey-mlx's convert/converter.py:
      * rename refiner.stem_conv.X     -> refiner.stem.0.X
      * rename refiner.stem_gn.X       -> refiner.stem.1.X
      * transpose conv weights (O,H,W,I) -> (O,I,H,W)
      * num_batches_tracked is absent in MLX; strict=False accepts it
  - (PyTorch only) load a local .pth checkpoint (dev escape hatch)

MLX engine usage:
    from engines._weights_loader import get_mlx_safetensors_path
    path = get_mlx_safetensors_path()   # downloads if cache empty

PyTorch engine usage:
    from engines._weights_loader import (
        resolve_pytorch_weights, load_pytorch_state_dict,
    )
    located = resolve_pytorch_weights()
    path, fmt = located
    sd = load_pytorch_state_dict(path, fmt)
"""

from __future__ import annotations

import logging
import os
import sys
import urllib.request
from pathlib import Path

logger = logging.getLogger("corridorkey.engines.weights")


# ---------------------------------------------------------------------------
# Release source of truth
# ---------------------------------------------------------------------------

CORRIDORKEY_MLX_RELEASE_URL = (
    "https://github.com/nikopueringer/corridorkey-mlx/releases/download/"
    "v1.0.0/corridorkey_mlx.safetensors"
)
WEIGHT_FILENAME = "corridorkey_mlx.safetensors"


# ---------------------------------------------------------------------------
# Cache directory
# ---------------------------------------------------------------------------

def _user_cache_dir() -> Path:
    """Return a per-user cache directory for CorridorKey weights.

    Uses native locations per platform so that users following each
    OS's conventions find the cache where they expect.
    """
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local")))
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local" / "share")))
    return base / "CorridorKey" / "models"


def cached_weights_path() -> Path:
    """Return the canonical cache path for the MLX safetensors file.

    Does NOT check existence — call `.exists()` yourself.
    """
    return _user_cache_dir() / WEIGHT_FILENAME


# ---------------------------------------------------------------------------
# Downloader
# ---------------------------------------------------------------------------

def _download_mlx_safetensors(dest: Path) -> Path | None:
    """Download the official MLX safetensors release. ~398 MB.

    Writes to `<dest>.part` and atomically renames on success so a
    cancelled download doesn't leave a half-file masquerading as valid.
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    logger.info("Downloading CorridorKey weights from %s", CORRIDORKEY_MLX_RELEASE_URL)
    logger.info("This is a one-time ~398 MB download.")
    try:
        with urllib.request.urlopen(CORRIDORKEY_MLX_RELEASE_URL) as resp:
            total = int(resp.headers.get("Content-Length", "0"))
            with open(tmp, "wb") as f:
                downloaded = 0
                last_log = 0
                while True:
                    chunk = resp.read(1024 * 1024)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total and downloaded - last_log > 32 * 1024 * 1024:
                        pct = 100.0 * downloaded / total
                        logger.info(
                            "  ... %.0f%% (%d / %d MB)",
                            pct, downloaded // (1024 * 1024), total // (1024 * 1024),
                        )
                        last_log = downloaded
        tmp.rename(dest)
        logger.info("Downloaded weights to %s", dest)
        return dest
    except Exception as e:
        logger.exception("Weight download failed: %s", e)
        if tmp.exists():
            tmp.unlink(missing_ok=True)
        return None


def get_mlx_safetensors_path(allow_download: bool = True) -> Path | None:
    """Shared entry point for both engines. Returns the path to the cached
    `corridorkey_mlx.safetensors`, downloading it on first run if needed.

    Returns None if the cache is empty and either `allow_download=False`
    or the download failed.
    """
    cached = cached_weights_path()
    if cached.exists():
        logger.info("Using cached weights: %s", cached)
        return cached
    if not allow_download:
        return None
    return _download_mlx_safetensors(cached)


# ---------------------------------------------------------------------------
# PyTorch-specific: MLX safetensors -> state_dict reverse conversion
# ---------------------------------------------------------------------------

# Inverse of corridorkey-mlx's REFINER_STEM_MAP.
_REFINER_STEM_REVERSE_MAP = {
    "refiner.stem_conv.weight": "refiner.stem.0.weight",
    "refiner.stem_conv.bias":   "refiner.stem.0.bias",
    "refiner.stem_gn.weight":   "refiner.stem.1.weight",
    "refiner.stem_gn.bias":     "refiner.stem.1.bias",
}

# Same set as upstream CONV_WEIGHT_KEYS, but referenced by the destination
# (PyTorch) name. These were transposed (O,I,H,W) -> (O,H,W,I) on MLX export
# and need to be transposed back here.
_PYTORCH_CONV_WEIGHT_KEYS: frozenset[str] = frozenset(
    [
        "encoder.model.patch_embed.proj.weight",
        "alpha_decoder.linear_fuse.weight",
        "alpha_decoder.classifier.weight",
        "fg_decoder.linear_fuse.weight",
        "fg_decoder.classifier.weight",
        "refiner.stem.0.weight",
        "refiner.final.weight",
    ]
    + [f"refiner.res{i}.conv{j}.weight" for i in range(1, 5) for j in range(1, 3)]
)


def _convert_mlx_safetensors_to_pytorch(path: Path) -> dict:
    """Load an MLX-format safetensors file and return a PyTorch state_dict.

    Reverse of nikopueringer/corridorkey-mlx convert/converter.py.
    """
    import torch
    from safetensors.numpy import load_file

    raw = load_file(str(path))
    state_dict: dict = {}
    for src_key, arr in raw.items():
        dest_key = _REFINER_STEM_REVERSE_MAP.get(src_key, src_key)
        if dest_key in _PYTORCH_CONV_WEIGHT_KEYS:
            # MLX (O, H, W, I) -> PyTorch (O, I, H, W)
            arr = arr.transpose(0, 3, 1, 2)
        state_dict[dest_key] = torch.from_numpy(arr.copy())
    return state_dict


def _load_pytorch_pth(path: Path) -> dict:
    """Load a .pth checkpoint (CORRIDORKEY_PT_WEIGHTS escape hatch).

    Strips the `_orig_mod.` torch.compile prefix from state_dict keys.
    """
    import torch

    raw = torch.load(str(path), map_location="cpu", weights_only=False)
    sd = raw.get("state_dict", raw) if isinstance(raw, dict) else raw
    cleaned = {}
    for k, v in sd.items():
        if k.startswith("_orig_mod."):
            cleaned[k[len("_orig_mod."):]] = v
        else:
            cleaned[k] = v
    return cleaned


# ---------------------------------------------------------------------------
# PyTorch engine public API
# ---------------------------------------------------------------------------

def resolve_pytorch_weights(allow_download: bool = True) -> tuple[Path, str] | None:
    """Locate PyTorch-loadable weights, downloading if necessary.

    Discovery order:
      1. CORRIDORKEY_PT_WEIGHTS env var (escape hatch — local .pth or .safetensors)
      2. Shared cached MLX safetensors download

    Returns (path, format) where format is "pth" or "safetensors",
    or None if nothing could be obtained.
    """
    env = os.environ.get("CORRIDORKEY_PT_WEIGHTS")
    if env:
        p = Path(env)
        if p.exists():
            fmt = "safetensors" if p.suffix == ".safetensors" else "pth"
            logger.info("Using CORRIDORKEY_PT_WEIGHTS: %s (%s)", p, fmt)
            return (p, fmt)
        logger.warning("CORRIDORKEY_PT_WEIGHTS set but not found: %s", env)

    cached = get_mlx_safetensors_path(allow_download=allow_download)
    if cached is not None:
        return (cached, "safetensors")
    return None


def load_pytorch_state_dict(path: Path, fmt: str) -> dict:
    """Load a state_dict from disk in either supported format."""
    if fmt == "pth":
        return _load_pytorch_pth(path)
    if fmt == "safetensors":
        return _convert_mlx_safetensors_to_pytorch(path)
    raise ValueError(f"Unknown weight format: {fmt}")

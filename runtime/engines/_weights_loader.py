"""
Weights discovery + loading for the PyTorch CorridorKey engine.

Two sources, tried in order:

  1. CORRIDORKEY_PT_WEIGHTS env var pointing at a local .pth or .safetensors
     (escape hatch for development / custom checkpoints).
  2. Cached download of the official MLX-format safetensors from
     github.com/nikopueringer/corridorkey-mlx Releases. The MLX format is a
     deterministic transform of the original PyTorch state_dict; we apply
     the inverse here to load it into a PyTorch GreenFormer.

The cached download lives in <user data dir>/CorridorKey/models/ — usually
%LOCALAPPDATA%\\CorridorKey\\models on Windows. Downloads happen on first
run if the cache is empty. CorridorKey AE is fully self-hosting via the
upstream corridorkey-mlx release — no external tool needs to be installed.

The reverse converter (MLX -> PyTorch state_dict) is the inverse of
nikopueringer/corridorkey-mlx's convert/converter.py:

  - rename refiner.stem_conv.X      -> refiner.stem.0.X
  - rename refiner.stem_gn.X        -> refiner.stem.1.X
  - transpose conv weights (O,H,W,I) -> (O,I,H,W)
  - num_batches_tracked is missing in the MLX format; PyTorch BatchNorm
    accepts strict=False loads where this is absent.
"""

from __future__ import annotations

import logging
import os
import urllib.request
from pathlib import Path

logger = logging.getLogger("corridorkey.engines.weights")


def _user_cache_dir() -> Path:
    """Return a per-user cache directory for CorridorKey weights."""
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local")))
    elif "darwin" in os.sys.platform:  # type: ignore[attr-defined]
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local" / "share")))
    return base / "CorridorKey" / "models"


# ---------------------------------------------------------------------------
# MLX safetensors downloader
# ---------------------------------------------------------------------------

CORRIDORKEY_MLX_RELEASE_URL = (
    "https://github.com/nikopueringer/corridorkey-mlx/releases/download/"
    "v1.0.0/corridorkey_mlx.safetensors"
)
EXPECTED_SAFETENSORS_NAME = "corridorkey_mlx.safetensors"


def _download_mlx_safetensors(dest: Path) -> Path | None:
    """Download the official MLX safetensors release. ~398 MB."""
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


# ---------------------------------------------------------------------------
# MLX -> PyTorch state_dict reverse conversion
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
        # Reverse refiner stem renames
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
# Public API
# ---------------------------------------------------------------------------

def find_or_download_weights(
    allow_download: bool = True,
) -> tuple[Path, str] | None:
    """Locate weights on disk, downloading if necessary.

    Returns:
        Tuple of (path, format) where format is "pth" or "safetensors", or
        None if no weights could be obtained.
    """
    # 1. Explicit override (escape hatch for development / custom weights)
    env = os.environ.get("CORRIDORKEY_PT_WEIGHTS")
    if env:
        p = Path(env)
        if p.exists():
            fmt = "safetensors" if p.suffix == ".safetensors" else "pth"
            logger.info("Using CORRIDORKEY_PT_WEIGHTS: %s (%s)", p, fmt)
            return (p, fmt)
        logger.warning("CORRIDORKEY_PT_WEIGHTS set but not found: %s", env)

    # 2. Cached download (canonical source)
    cached = _user_cache_dir() / EXPECTED_SAFETENSORS_NAME
    if cached.exists():
        logger.info("Using cached weights: %s", cached)
        return (cached, "safetensors")

    # 3. Download
    if allow_download:
        downloaded = _download_mlx_safetensors(cached)
        if downloaded is not None:
            return (downloaded, "safetensors")

    return None


def load_state_dict_from_path(path: Path, fmt: str) -> dict:
    """Load a state_dict from disk in either supported format."""
    if fmt == "pth":
        return _load_pytorch_pth(path)
    if fmt == "safetensors":
        return _convert_mlx_safetensors_to_pytorch(path)
    raise ValueError(f"Unknown weight format: {fmt}")

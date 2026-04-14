"""Tests for the shared weight-loader module.

These tests avoid hitting the network and don't require any real model
weights on disk — they exercise the pure-logic bits: cache-path
selection, the MLX->PyTorch reverse converter key mapping, and the
resolve/escape-hatch plumbing.
"""

from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import pytest
from engines import _weights_loader as wl

# ---------------------------------------------------------------------------
# Cache path
# ---------------------------------------------------------------------------

def test_cache_path_ends_with_expected_filename():
    p = wl.cached_weights_path()
    assert p.name == wl.WEIGHT_FILENAME
    assert p.parent.name == "models"
    assert p.parent.parent.name == "CorridorKey"


def test_cache_path_is_per_user(tmp_path, monkeypatch):
    """On Windows the cache should root under LOCALAPPDATA."""
    if os.name != "nt":
        pytest.skip("LOCALAPPDATA-specific test")
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    p = wl._user_cache_dir()
    assert str(p).startswith(str(tmp_path))


# ---------------------------------------------------------------------------
# Reverse MLX->PyTorch converter
# ---------------------------------------------------------------------------

def test_refiner_stem_key_rename():
    """Stem keys get Sequential-index form, other refiner keys are unchanged."""
    reverse = wl._REFINER_STEM_REVERSE_MAP
    assert reverse["refiner.stem_conv.weight"] == "refiner.stem.0.weight"
    assert reverse["refiner.stem_conv.bias"]   == "refiner.stem.0.bias"
    assert reverse["refiner.stem_gn.weight"]   == "refiner.stem.1.weight"
    assert reverse["refiner.stem_gn.bias"]     == "refiner.stem.1.bias"


def test_conv_weight_key_set_matches_upstream():
    """All expected conv keys appear in the transpose set."""
    expected_static = {
        "encoder.model.patch_embed.proj.weight",
        "alpha_decoder.linear_fuse.weight",
        "alpha_decoder.classifier.weight",
        "fg_decoder.linear_fuse.weight",
        "fg_decoder.classifier.weight",
        "refiner.stem.0.weight",
        "refiner.final.weight",
    }
    for k in expected_static:
        assert k in wl._PYTORCH_CONV_WEIGHT_KEYS, f"missing key: {k}"

    # All 8 dilated residual conv weights
    for i in range(1, 5):
        for j in range(1, 3):
            key = f"refiner.res{i}.conv{j}.weight"
            assert key in wl._PYTORCH_CONV_WEIGHT_KEYS, f"missing refiner key: {key}"


def test_reverse_conversion_shape_roundtrip(tmp_path):
    """Write a tiny MLX-format safetensors + reverse-convert it.

    Confirms the key rename + conv transpose math is correct without
    needing a real 400 MB checkpoint or a GPU.
    """
    from safetensors.numpy import save_file

    # Two stem keys that need renaming + transposing
    # MLX layout: conv weights are (O, H, W, I)
    stem_conv_mlx = np.random.rand(64, 3, 3, 7).astype(np.float32)  # (O=64, H=3, W=3, I=7)
    stem_gn_mlx   = np.random.rand(64).astype(np.float32)

    path = tmp_path / "fake.safetensors"
    save_file(
        {
            "refiner.stem_conv.weight": stem_conv_mlx,
            "refiner.stem_conv.bias":   np.random.rand(64).astype(np.float32),
            "refiner.stem_gn.weight":   stem_gn_mlx,
            "refiner.stem_gn.bias":     np.random.rand(64).astype(np.float32),
            # A non-conv key that should pass through unchanged
            "refiner.res1.gn1.weight":  np.random.rand(64).astype(np.float32),
        },
        str(path),
    )

    sd = wl._convert_mlx_safetensors_to_pytorch(path)

    # Keys were renamed
    assert "refiner.stem.0.weight" in sd
    assert "refiner.stem.0.bias"   in sd
    assert "refiner.stem.1.weight" in sd
    assert "refiner.stem.1.bias"   in sd
    assert "refiner.stem_conv.weight" not in sd
    assert "refiner.stem_gn.weight"   not in sd

    # Conv weight was transposed: MLX (O,H,W,I) -> PyTorch (O,I,H,W)
    pt_conv = sd["refiner.stem.0.weight"].numpy()
    assert pt_conv.shape == (64, 7, 3, 3), (
        f"expected (O,I,H,W)=(64,7,3,3), got {pt_conv.shape}"
    )
    np.testing.assert_allclose(pt_conv, np.transpose(stem_conv_mlx, (0, 3, 1, 2)))

    # GN weight passed through unchanged
    np.testing.assert_allclose(sd["refiner.stem.1.weight"].numpy(), stem_gn_mlx)

    # Non-matching key passed through
    assert "refiner.res1.gn1.weight" in sd


# ---------------------------------------------------------------------------
# Resolution / escape hatch
# ---------------------------------------------------------------------------

def test_get_mlx_safetensors_path_no_download_returns_none_when_missing(
    tmp_path, monkeypatch,
):
    """When cache is empty and downloading is disabled, return None gracefully."""
    fake_cache = tmp_path / "fake-cache"
    monkeypatch.setattr(wl, "cached_weights_path", lambda: fake_cache / wl.WEIGHT_FILENAME)
    assert wl.get_mlx_safetensors_path(allow_download=False) is None


def test_get_mlx_safetensors_path_returns_existing_file(tmp_path, monkeypatch):
    fake = tmp_path / wl.WEIGHT_FILENAME
    fake.write_bytes(b"not really a safetensors file but exists()")
    monkeypatch.setattr(wl, "cached_weights_path", lambda: fake)
    assert wl.get_mlx_safetensors_path(allow_download=False) == fake


def test_resolve_pytorch_weights_env_override(tmp_path, monkeypatch):
    """CORRIDORKEY_PT_WEIGHTS should win over the cache."""
    fake = tmp_path / "dev-checkpoint.pth"
    fake.write_bytes(b"stub")
    monkeypatch.setenv("CORRIDORKEY_PT_WEIGHTS", str(fake))
    located = wl.resolve_pytorch_weights(allow_download=False)
    assert located is not None
    path, fmt = located
    assert Path(path) == fake
    assert fmt == "pth"


def test_resolve_pytorch_weights_env_safetensors_format(tmp_path, monkeypatch):
    """Env override with .safetensors extension should be detected as such."""
    fake = tmp_path / "dev-checkpoint.safetensors"
    fake.write_bytes(b"stub")
    monkeypatch.setenv("CORRIDORKEY_PT_WEIGHTS", str(fake))
    located = wl.resolve_pytorch_weights(allow_download=False)
    assert located is not None
    _, fmt = located
    assert fmt == "safetensors"

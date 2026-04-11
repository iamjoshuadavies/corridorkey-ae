"""Tests for the model manager."""

import tempfile
from pathlib import Path

from models.manager import ModelManager


def test_empty_models_dir():
    with tempfile.TemporaryDirectory() as tmpdir:
        mgr = ModelManager(models_dir=Path(tmpdir))
        assert mgr.list_models() == []
        assert not mgr.is_model_available("test.pt")


def test_model_available_after_create():
    with tempfile.TemporaryDirectory() as tmpdir:
        models_dir = Path(tmpdir)
        (models_dir / "test.pt").write_bytes(b"fake model data")
        mgr = ModelManager(models_dir=models_dir)
        assert mgr.is_model_available("test.pt")
        assert "test.pt" in mgr.list_models()


def test_verify_model_checksum():
    import hashlib

    with tempfile.TemporaryDirectory() as tmpdir:
        models_dir = Path(tmpdir)
        data = b"test model data for checksum"
        (models_dir / "test.pt").write_bytes(data)
        expected = hashlib.sha256(data).hexdigest()

        mgr = ModelManager(models_dir=models_dir)
        assert mgr.verify_model("test.pt", expected)
        assert not mgr.verify_model("test.pt", "wrong_hash")

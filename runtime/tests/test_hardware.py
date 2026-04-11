"""Tests for hardware detection."""

from server.hardware import detect_hardware


def test_detect_hardware_returns_dict():
    info = detect_hardware()
    assert isinstance(info, dict)
    assert "device_name" in info
    assert "device_type" in info
    assert info["device_type"] in ("cpu", "cuda", "mps", "mlx")


def test_detect_hardware_has_memory_fields():
    info = detect_hardware()
    assert "vram_total_mb" in info
    assert isinstance(info["vram_total_mb"], int)
    assert "is_unified_memory" in info

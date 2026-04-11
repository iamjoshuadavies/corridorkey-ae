"""Tests for the request handler."""

from server.handler import RequestHandler


def test_ping():
    handler = RequestHandler()
    resp = handler.handle({"type": "ping"})
    assert resp["type"] == "pong"
    assert "version" in resp


def test_status():
    handler = RequestHandler()
    resp = handler.handle({"type": "status"})
    assert resp["type"] == "status"
    assert "device" in resp
    assert "model_state" in resp


def test_unknown_type():
    handler = RequestHandler()
    resp = handler.handle({"type": "nonsense"})
    assert resp["type"] == "error"


def test_process_frame_not_implemented():
    handler = RequestHandler()
    resp = handler.handle({"type": "process_frame", "width": 100, "height": 100})
    assert resp["type"] == "frame_result"
    assert resp["success"] is False

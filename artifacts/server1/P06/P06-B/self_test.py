"""Packet-local RED/GREEN contract test for the P06-B origin fixtures."""

from __future__ import annotations

import json
import socket
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = next(parent for parent in Path(__file__).resolve().parents if (parent / "tools").is_dir())
sys.path.insert(0, str(ROOT))

from tools.server_lab.fixtures.http_server import HttpOriginFixturePort
from tools.server_lab.fixtures.tracker_server import TrackerFixturePort


def _get(url: str) -> tuple[int, bytes, dict[str, str]]:
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return response.status, response.read(), dict(response.headers)
    except urllib.error.HTTPError as error:
        return error.code, error.read(), dict(error.headers)


def _jsonable(value):
    if isinstance(value, bytes):
        return {"bytes_hex": value.hex()}
    if isinstance(value, dict):
        return {key: _jsonable(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_jsonable(item) for item in value]
    return value


def main() -> int:
    root = Path(__file__).resolve().parent
    cancellation = {"generation": 7, "token": "p06-red-token"}
    tracker = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "cancellation": cancellation})
    origin = HttpOriginFixturePort({"metadata": b"metadata", "block": b"block", "cancellation": cancellation})
    tracker.start()
    origin.start()
    try:
        status, body, _ = _get(tracker.url("/announce?info_hash=abc&peer_id=peer"))
        assert status == 200 and body, (status, body)
        status, body, headers = _get(origin.url("/metadata?generation=7&token=p06-red-token"))
        assert status == 200 and body == b"metadata" and headers["X-Fixture-Generation"] == "7"
        assert tracker.events and origin.events
        # Tracker fault controls are instance-local and observable in exact order.
        no_peers = TrackerFixturePort({"no_peers": True}).start()
        try:
            _, no_peer_body, _ = _get(no_peers.url("/announce"))
            assert b"5:peers0:" in no_peer_body
        finally:
            no_peers.stop()
        duplicate = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "duplicate_peers": True}).start()
        try:
            _, duplicate_body, _ = _get(duplicate.url("/announce"))
            assert duplicate_body.count(b"127") == 0  # compact peer bytes are not textual
            assert any(event.get("response_bytes") for event in duplicate.events)
        finally:
            duplicate.stop()
        delayed = TrackerFixturePort({"delay_ms": 25}).start()
        try:
            started = time.monotonic()
            _get(delayed.url("/announce"))
            assert time.monotonic() - started >= 0.02
        finally:
            delayed.stop()
        fast = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "peer_becomes_fast": True}).start()
        try:
            assert b"5:peers0:" in _get(fast.url("/announce"))[1]
            assert b"5:peers0:" not in _get(fast.url("/announce"))[1]
        finally:
            fast.stop()

        # HTTP block/status/reset/cancellation controls remain on this origin socket.
        failing_block = HttpOriginFixturePort({"block_failure": True}).start()
        try:
            assert _get(failing_block.url("/block"))[0] == 503
        finally:
            failing_block.stop()
        failing_http = HttpOriginFixturePort({"http_failure": True}).start()
        try:
            assert _get(failing_http.url("/metadata"))[0] == 500
        finally:
            failing_http.stop()
        reset = HttpOriginFixturePort({"connection_reset": True}).start()
        try:
            try:
                urllib.request.urlopen(reset.url("/media"), timeout=3)
            except (ConnectionError, OSError, urllib.error.URLError):
                pass
            else:
                raise AssertionError("connection reset fault returned a response")
        finally:
            reset.stop()
        origin.cancel(7, "p06-red-token")
        assert _get(origin.url("/metadata?generation=7&token=p06-red-token"))[0] == 499
        receipt = {
            "case": "P06-02",
            "status": "PASS",
            "request_order": [event["operation"] for event in tracker.events + origin.events],
            "cancellation": cancellation,
            "ports": {"tracker": tracker.port, "http": origin.port},
            "replay_command": ["python", str(Path(__file__).name)],
            "configuration": {"tracker": _jsonable(tracker.config.__dict__), "http": _jsonable(origin.config)},
        }
        (root / "SELF-TEST.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        return 0
    finally:
        tracker.stop()
        origin.stop()


if __name__ == "__main__":
    raise SystemExit(main())

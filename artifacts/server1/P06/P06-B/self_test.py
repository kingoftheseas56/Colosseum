"""Packet-local RED/GREEN contract test for the P06-B origin fixtures."""

from __future__ import annotations

import json
import socket
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
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


def _assert_no_listener(port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.5)
        assert probe.connect_ex(("127.0.0.1", port)) != 0, f"listener remains on port {port}"


def _stop_cleanly(fixture) -> None:
    port = fixture.port
    fixture.stop()
    assert fixture.port == 0
    _assert_no_listener(port)


class _BarrierLock:
    def __init__(self, parties: int):
        self._barrier = threading.Barrier(parties)
        self._lock = threading.Lock()

    def __enter__(self):
        self._barrier.wait(timeout=30)
        self._lock.acquire()
        return self

    def __exit__(self, *_args):
        self._lock.release()


def _assert_record_ledger_is_atomic(fixture) -> None:
    fixture._lock = _BarrierLock(32)
    with ThreadPoolExecutor(max_workers=32) as pool:
        list(pool.map(lambda index: fixture._record("synthetic", index=index), range(32)))
    sequences = [event["sequence"] for event in fixture.events]
    assert sequences == list(range(32))


def main() -> int:
    root = Path(__file__).resolve().parent
    cancellation = {"generation": 7, "token": "p06-red-token"}
    _assert_record_ledger_is_atomic(TrackerFixturePort())
    _assert_record_ledger_is_atomic(HttpOriginFixturePort())
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
            _stop_cleanly(no_peers)
        duplicate = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "duplicate_peers": True}).start()
        try:
            _, duplicate_body, _ = _get(duplicate.url("/announce"))
            compact_peer = bytes((127, 0, 0, 1)) + (49001).to_bytes(2, "big")
            assert b"5:peers12:" + compact_peer + compact_peer + b"e" in duplicate_body
        finally:
            _stop_cleanly(duplicate)
        delayed = TrackerFixturePort({"delay_ms": 25}).start()
        try:
            started = time.monotonic()
            _get(delayed.url("/announce"))
            assert time.monotonic() - started >= 0.02
        finally:
            _stop_cleanly(delayed)
        fast = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "peer_becomes_fast": True}).start()
        try:
            assert b"5:peers0:" in _get(fast.url("/announce"))[1]
            assert b"5:peers0:" not in _get(fast.url("/announce"))[1]
        finally:
            _stop_cleanly(fast)

        concurrent = TrackerFixturePort({"delay_ms": 10}).start()
        try:
            barrier = threading.Barrier(8)

            def concurrent_request(_: int) -> int:
                barrier.wait(timeout=30)
                return _get(concurrent.url("/announce"))[0]

            with ThreadPoolExecutor(max_workers=8) as pool:
                assert list(pool.map(concurrent_request, range(8))) == [200] * 8
            sequences = [event["sequence"] for event in concurrent.events]
            assert sequences == list(range(len(sequences)))
            assert len({event["sequence"] for event in concurrent.events}) == len(sequences)
        finally:
            _stop_cleanly(concurrent)

        # HTTP block/status/reset/cancellation controls remain on this origin socket.
        failing_block = HttpOriginFixturePort({"block_failure": True}).start()
        try:
            assert _get(failing_block.url("/block"))[0] == 503
        finally:
            _stop_cleanly(failing_block)
        failing_http = HttpOriginFixturePort({"http_failure": True}).start()
        try:
            assert _get(failing_http.url("/metadata"))[0] == 500
        finally:
            _stop_cleanly(failing_http)
        delayed_http = HttpOriginFixturePort({"delayed_metadata_ms": 50}).start()
        try:
            started = time.monotonic()
            assert _get(delayed_http.url("/metadata"))[0] == 200
            assert time.monotonic() - started >= 0.04
            assert any(event.get("operation") == "delay" and event.get("delay_ms") == 50 for event in delayed_http.events)
        finally:
            _stop_cleanly(delayed_http)
        reset = HttpOriginFixturePort({"connection_reset": True}).start()
        try:
            try:
                urllib.request.urlopen(reset.url("/media"), timeout=3)
            except (ConnectionError, OSError, urllib.error.URLError):
                pass
            else:
                raise AssertionError("connection reset fault returned a response")
        finally:
            _stop_cleanly(reset)
        origin.cancel(7, "p06-red-token")
        assert _get(origin.url("/metadata?generation=7&token=p06-red-token"))[0] == 499
        receipt = {
            "schema": "colosseum-server1-p06-b-self-test/v2",
            "case": "P06-02",
            "status": "PASS",
            "states": {
                "authored": "PASS",
                "tested": "PASS",
                "executed": "PASS",
                "integrated": "NOT_RUN",
            },
            "checks": {
                "duplicate_compact_peers": "PASS",
                "http_delayed_metadata_ms": "PASS",
                "concurrent_ledger_sequence": "PASS",
                "clean_shutdown_no_listener": "PASS",
            },
            "request_order": [event["operation"] for event in tracker.events + origin.events],
            "cancellation": cancellation,
            "ports": {"tracker": tracker.port, "http": origin.port},
            "replay_command": ["python", "artifacts/server1/P06/P06-B/self_test.py"],
            "replay_cwd": "repository root",
            "configuration": {"tracker": _jsonable(tracker.config.__dict__), "http": _jsonable(origin.config)},
        }
        (root / "SELF-TEST.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        return 0
    finally:
        tracker.stop()
        origin.stop()


if __name__ == "__main__":
    raise SystemExit(main())

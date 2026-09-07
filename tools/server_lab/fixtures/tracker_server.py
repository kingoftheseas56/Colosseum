"""Deterministic loopback tracker fixture for the Server 1.0 lab."""

from __future__ import annotations

import argparse
import json
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlsplit


def _bencode(value: Any) -> bytes:
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    if isinstance(value, str):
        return _bencode(value.encode())
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, list):
        return b"l" + b"".join(_bencode(item) for item in value) + b"e"
    if isinstance(value, dict):
        return b"d" + b"".join(_bencode(str(key)) + _bencode(item) for key, item in sorted(value.items())) + b"e"
    raise TypeError(f"unsupported bencode value: {type(value)!r}")


@dataclass
class TrackerConfig:
    peers: list[list[Any]]
    interval: int = 30
    delay_ms: int = 0
    no_peers: bool = False
    duplicate_peers: bool = False
    peer_becomes_fast: bool = False
    fast_after_requests: int = 1
    cancellation: dict[str, Any] | None = None
    event_file: str | None = None

    @classmethod
    def from_value(cls, value: dict[str, Any] | None) -> "TrackerConfig":
        value = value or {}
        return cls(
            peers=value.get("peers", [["127.0.0.1", 49001]]),
            interval=int(value.get("interval", 30)),
            delay_ms=int(value.get("delay_ms", value.get("delayed_metadata_ms", 0))),
            no_peers=bool(value.get("no_peers", False)),
            duplicate_peers=bool(value.get("duplicate_peers", False)),
            peer_becomes_fast=bool(value.get("peer_becomes_fast", False)),
            fast_after_requests=int(value.get("fast_after_requests", 1)),
            cancellation=value.get("cancellation"),
            event_file=value.get("event_file"),
        )


class TrackerFixturePort:
    def __init__(self, config: dict[str, Any] | TrackerConfig | None = None):
        self.config = config if isinstance(config, TrackerConfig) else TrackerConfig.from_value(config)
        self.events: list[dict[str, Any]] = []
        self._lock = threading.Lock()
        self._requests = 0
        self._cancelled: dict[str, Any] | None = None
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    @property
    def port(self) -> int:
        return self._server.server_address[1] if self._server else 0

    @property
    def endpoint(self) -> str:
        return f"127.0.0.1:{self.port}"

    def url(self, path: str = "/announce") -> str:
        return f"http://127.0.0.1:{self.port}{path}"

    def start(self) -> "TrackerFixturePort":
        if self._server:
            return self
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802
                fixture._handle(self)

            def log_message(self, *_args: Any) -> None:
                return

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self._server.daemon_threads = True
        self._thread = threading.Thread(target=self._server.serve_forever, name="p06-tracker", daemon=True)
        self._thread.start()
        self._record("ready", endpoint=self.endpoint)
        return self

    def stop(self) -> None:
        if not self._server:
            return
        self._record("shutdown", endpoint=self.endpoint)
        self._server.shutdown()
        self._server.server_close()
        if self._thread:
            self._thread.join(timeout=3)
        self._server = None
        self._thread = None

    def cancel(self, generation: int, token: str) -> None:
        self._cancelled = {"generation": generation, "token": token}
        self._record("cancel", generation=generation, token=token)

    def _handle(self, request: BaseHTTPRequestHandler) -> None:
        parsed = urlsplit(request.path)
        query = parse_qs(parsed.query, keep_blank_values=True)
        generation = int(query.get("generation", [0])[0] or 0)
        token = query.get("token", [""])[0]
        self._requests += 1
        self._record("request", method="GET", path=parsed.path, query=parsed.query, generation=generation, token=token)
        if self._cancelled and (generation, token) == (self._cancelled.get("generation"), self._cancelled.get("token")):
            self._respond(request, 499, b"cancelled", {"Content-Type": "text/plain"})
            self._record("request_after_cancellation", generation=generation, token=token)
            return
        if self.config.delay_ms:
            self._record("delay", delay_ms=self.config.delay_ms)
            time.sleep(self.config.delay_ms / 1000)
        peers = [] if self.config.no_peers else list(self.config.peers)
        if self.config.peer_becomes_fast and self._requests <= self.config.fast_after_requests:
            peers = []
        if self.config.duplicate_peers:
            peers = peers + peers
        compact = b"".join(bytes(map(int, str(host).split("."))) + int(port).to_bytes(2, "big") for host, port in peers)
        body = _bencode({"interval": self.config.interval, "peers": compact})
        self._respond(request, 200, body, {"Content-Type": "text/plain", "Content-Length": str(len(body))})

    def _respond(self, request: BaseHTTPRequestHandler, status: int, body: bytes, headers: dict[str, str]) -> None:
        request.send_response(status)
        for key, value in headers.items():
            request.send_header(key, value)
        request.end_headers()
        request.wfile.write(body)
        self._record("response", status=status, response_bytes=body.hex(), headers=headers)

    def _record(self, operation: str, **fields: Any) -> None:
        event = {"sequence": len(self.events), "operation": operation, **fields}
        with self._lock:
            self.events.append(event)
            if self.config.event_file:
                path = Path(self.config.event_file)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(event, sort_keys=True) + "\n")


TrackerServer = TrackerFixturePort


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    fixture = TrackerFixturePort(json.loads(args.config.read_text(encoding="utf-8"))).start()
    print(json.dumps({"endpoint": fixture.endpoint, "url": fixture.url(), "replay_config": str(args.config)}), flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        fixture.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Controlled loopback HTTP origin fixture for Server 1.0 lab cases."""

from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlsplit


class HttpOriginFixturePort:
    def __init__(self, config: dict[str, Any] | None = None):
        self.config = config or {}
        self.events: list[dict[str, Any]] = []
        self._lock = threading.Lock()
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self._cancelled: dict[str, Any] | None = None

    @property
    def port(self) -> int:
        return self._server.server_address[1] if self._server else 0

    @property
    def endpoint(self) -> str:
        return f"127.0.0.1:{self.port}"

    def url(self, path: str = "/metadata") -> str:
        return f"http://127.0.0.1:{self.port}{path}"

    def start(self) -> "HttpOriginFixturePort":
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
        self._thread = threading.Thread(target=self._server.serve_forever, name="p06-http-origin", daemon=True)
        self._thread.start()
        self._record("ready", endpoint=self.endpoint)
        return self

    def stop(self) -> None:
        if not self._server:
            return
        server = self._server
        thread = self._thread
        self._record("shutdown", endpoint=self.endpoint)
        server.shutdown()
        server.server_close()
        if thread:
            thread.join(timeout=3)
            if thread.is_alive():
                self._record("shutdown_timeout", endpoint=self.endpoint)
                raise RuntimeError("HTTP origin fixture server thread did not terminate")
        self._server = None
        self._thread = None

    def cancel(self, generation: int, token: str) -> None:
        with self._lock:
            self._cancelled = {"generation": generation, "token": token}
        self._record("cancel", generation=generation, token=token)

    def _handle(self, request: BaseHTTPRequestHandler) -> None:
        parsed = urlsplit(request.path)
        query = parse_qs(parsed.query, keep_blank_values=True)
        generation = int(query.get("generation", [0])[0] or 0)
        token = query.get("token", [""])[0]
        with self._lock:
            cancelled = dict(self._cancelled) if self._cancelled else None
        self._record("request", method="GET", path=parsed.path, query=parsed.query, generation=generation, token=token)
        if cancelled and (generation, token) == (cancelled.get("generation"), cancelled.get("token")):
            self._respond(request, 499, b"cancelled", {"Content-Type": "text/plain"})
            self._record("request_after_cancellation", generation=generation, token=token)
            return
        if self.config.get("connection_reset"):
            self._record("connection_reset")
            request.connection.shutdown(socket.SHUT_RDWR)
            request.connection.close()
            return
        if parsed.path == "/metadata" and self.config.get("delayed_metadata_ms", self.config.get("delay_ms", 0)):
            delay = int(self.config.get("delayed_metadata_ms", self.config.get("delay_ms", 0)))
            self._record("delay", delay_ms=delay)
            time.sleep(delay / 1000)
        if parsed.path == "/block" and self.config.get("block_failure"):
            self._respond(request, int(self.config.get("block_status", 503)), b"block-failure", {"Content-Type": "text/plain"})
            return
        if self.config.get("http_failure"):
            self._respond(request, int(self.config.get("http_status", 500)), b"http-failure", {"Content-Type": "text/plain"})
            return
        values = {"/metadata": b"metadata", "/block": b"block", "/media": b"media", "/health": b"ok"}
        configured = self.config.get(parsed.path.lstrip("/"), values.get(parsed.path, b""))
        body = configured.encode() if isinstance(configured, str) else bytes(configured)
        headers = {"Content-Type": "application/octet-stream", "Content-Length": str(len(body)), "X-Fixture-Generation": str(generation)}
        self._respond(request, 200, body, headers)

    def _respond(self, request: BaseHTTPRequestHandler, status: int, body: bytes, headers: dict[str, str]) -> None:
        request.send_response(status)
        for key, value in headers.items():
            request.send_header(key, value)
        request.end_headers()
        request.wfile.write(body)
        self._record("response", status=status, response_bytes=body.hex(), headers=headers)

    def _record(self, operation: str, **fields: Any) -> None:
        with self._lock:
            event = {"sequence": len(self.events), "operation": operation, **fields}
            self.events.append(event)
            event_file = self.config.get("event_file")
            if event_file:
                path = Path(event_file)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(event, sort_keys=True) + "\n")


HttpServer = HttpOriginFixturePort


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    fixture = HttpOriginFixturePort(json.loads(args.config.read_text(encoding="utf-8"))).start()
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

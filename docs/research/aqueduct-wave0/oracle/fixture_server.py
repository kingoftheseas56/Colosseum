#!/usr/bin/env python3
"""Deterministic localhost origin for Stremio oracle captures."""
from __future__ import annotations

import argparse
import mimetypes
import re
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

RANGE_RE = re.compile(r"bytes=(\d*)-(\d*)$")


class RangeHandler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def __init__(self, *args, directory: str, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def end_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def do_HEAD(self) -> None:
        self._serve(send_body=False)
    def do_GET(self) -> None:
        self._serve(send_body=True)

    def _serve(self, send_body: bool) -> None:
        target = Path(self.translate_path(self.path.split("?", 1)[0]))
        if not target.is_file():
            self.send_error(404)
            return
        size = target.stat().st_size
        start, end, status = 0, size - 1, 200
        raw_range = self.headers.get("Range")
        if raw_range:
            match = RANGE_RE.fullmatch(raw_range.strip())
            if not match:
                self.send_error(416)
                return
            left, right = match.groups()
            if left:
                start = int(left)
                end = int(right) if right else size - 1
            elif right:
                count = int(right)
                start = max(0, size - count)
            if start >= size or end < start:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.end_headers()
                return
            end = min(end, size - 1)
            status = 206
        length = end - start + 1
        self.send_response(status)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", mimetypes.guess_type(target.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if not send_body:
            return
        with target.open("rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining:
                chunk = handle.read(min(65536, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)


def create_server(root: Path, port: int = 0) -> ThreadingHTTPServer:
    handler = lambda *a, **kw: RangeHandler(*a, directory=str(root), **kw)
    return ThreadingHTTPServer(("127.0.0.1", port), handler)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    server = create_server(args.root.resolve(), args.port)
    host, port = server.server_address
    print(f"FIXTURE_SERVER http://{host}:{port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

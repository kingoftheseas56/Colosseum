from __future__ import annotations

import argparse
import base64
import http.server
import json
import struct
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any

VIDEO_ID = "function0008-direct-video"
EXPECTED_REQUEST_HEADERS = {
    "Referer": "https://origin.example/function0008",
    "Origin": "https://origin.example",
    "X-Function-0008": "runtime-proof",
}
NEGATIVE_HEADER = "X-Function-0008-Negative-Control"
NEGATIVE_VALUE = "response-only-do-not-forward"
FAST_PREFIX_BYTES = 8 * 1024 * 1024 + 768 * 1024
DOWNLOAD_CHUNK_BYTES = 256 * 1024
DOWNLOAD_CHUNK_DELAY_SECONDS = 0.015
MP4_BASE64 = """AAAAIGZ0eXBpc29tAAACAGlzb21pc28yYXZjMW1wNDEAAAN3bW9vdgAAAGxtdmhkAAAAAAAAAAAAAAAAAAFfkAAEHrAAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAqJ0cmFrAAAAXHRraGQAAAADAAAAAAAAAAAAAAABAAAAAAAEHrAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAUAAAAC0AAAAAAAkZWR0cwAAABxlbHN0AAAAAAAAAAEABB6wAAAAAAABAAAAAAIabWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAAFfkAAEHrBVxAAAAAAALWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABxW1pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAAYVzdGJsAAAAuXN0c2QAAAAAAAAAAQAAAKlhdmMxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAUAAtABIAAAASAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGP//AAAAL2F2Y0MBQsAL/+EAGGdCwAvaBQZ+fARAAAADAEAAAAMDI8UKqAEABGjOD8gAAAAQcGFzcAAAAAEAAAABAAAAFGJ0cnQAAAAAAAAt0AAALdAAAAAYc3R0cwAAAAAAAAABAAAAEgAAOpgAAAAgc3RzcwAAAAAAAAAEAAAAAQAAAAcAAAAKAAAAEAAAABxzdHNjAAAAAAAAAAEAAAABAAAAEgAAAAEAAABcc3RzegAAAAAAAAAAAAAAEgAAA5AAAAAoAAAAKQAAACkAAAApAAAAKQAAATUAAAAoAAAAKQAABeMAAABEAAAARAAAAEUAAABFAAAARQAAA4oAAABEAAAARAAAABRzdGNvAAAAAAAAAAEAAAOnAAAAYXVkdGEAAABZbWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAbWRpcmFwcGwAAAAAAAAAAAAAAAAsaWxzdAAAACSpdG9vAAAAHGRhdGEAAAABAAAAAExhdmY2My4xLjEwMQAAAAhmcmVlAAARNm1kYXQAAAACCfAAAAAYZ0LAC9oFBn58BEAAAAMAQAAAAwMjxQqoAAAABGjOD8gAAAJaBgX//1bcRem95tlIt5Ys2CDZI+7veDI2NCAtIGNvcmUgMTY1IHIzMjIzIDA0ODBjYjAgLSBILjI2NC9NUEVHLTQgQVZDIGNvZGVjIC0gQ29weWxlZnQgMjAwMy0yMDI1IC0gaHR0cDovL3d3dy52aWRlb2xhbi5vcmcveDI2NC5odG1sIC0gb3B0aW9uczogY2FiYWM9MCByZWY9MSBkZWJsb2NrPTA6MDowIGFuYWx5c2U9MDowIG1lPWRpYSBzdWJtZT0wIHBzeT0xIHBzeV9yZD0xLjAwOjAuMDAgbWl4ZWRfcmVmPTAgbWVfcmFuZ2U9MTYgY2hyb21hX21lPTEgdHJlbGxpcz0wIDh4OGRjdD0wIGNxbT0wIGRlYWR6b25lPTIxLDExIGZhc3RfcHNraXA9MSBjaHJvbWFfcXBfb2Zmc2V0PTAgdGhyZWFkcz0zIGxvb2thaGVhZF90aHJlYWRzPTMgc2xpY2VkX3RocmVhZHM9MSBzbGljZXM9MyBucj0wIGRlY2ltYXRlPTEgaW50ZXJsYWNlZD0wIGJsdXJheV9jb21wYXQ9MCBjb25zdHJhaW5lZF9pbnRyYT0wIGJmcmFtZXM9MCB3ZWlnaHRwPTAga2V5aW50PTYga2V5aW50X21pbj0xIHNjZW5lY3V0PTAgaW50cmFfcmVmcmVzaD0wIHJjPWNyZiBtYnRyZWU9MCBjcmY9MjMuMCBxY29tcD0wLjYwIHFwbWluPTAgcXBtYXg9NjkgcXBzdGVwPTQgaXBfcmF0aW89MS40MCBhcT0wAIAAAABTZYiEOhGKAAIJccAAQlI4AAhjScnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAABUZQKIiEOhGKAAIJccAAQlI4AAhjScnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAAVWUBQiIQ6EYoAAglxwABCUjgACGNJycnJycnJycnJycnJycnJycnJ1111111111111111111111111111111111111111111111111111111111114AAAAACCfAAAAAGQZogPoCjAAAACEECiaID6AowAAAACEEBQmiA+gKMAAAAAgnwAAAAB0GaQBCgKMAAAAAIQQKJpAEKAowAAAAIQQFCaQBCgKMAAAACCfAAAAAHQZpgEKAowAAAAAhBAommAQoCjAAAAAhBAUJpgEKAowAAAAIJ8AAAAAdBmoAQoCjAAAAACEECiagBCgKMAAAACEEBQmoAQoCjAAAAAgnwAAAAB0GaoBCgKMAAAAAIQQKJqgEKAowAAAAIQQFCaoBCgKMAAAACCfAAAAAYZ0LAC9oFBn58BEAAAAMAQAAAAwMjxQqoAAAABGjOD8gAAABUZYiCAQoRigACO/HAAEzaOAAKD0nJycnJycnJycnJycnJycnJycnXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXgAAAAVWUCiIggEKEYoAAjvxwABM2jgACg9JycnJycnJycnJycnJycnJycnJ1111111111111111111111111111111111111111111111111111111111114AAABWZQFCIggEKEYoAAjvxwABM2jgACg9JycnJycnJycnJycnJycnJycnJ1111111111111111111111111111111111111111111111111111111111114AAAAACCfAAAAAGQZogPoCjAAAACEECiaID6AowAAAACEEBQmiA+gKMAAAAAgnwAAAAB0GaQBCgKMAAAAAIQQKJpAEKAowAAAAIQQFCaQBCgKMAAAACCfAAAAAZZ0LAFtoCgL/lwEQAAAMABAAAAwAyPFi6gAAAAARozg/IAAACWgYF//9W3EXpvebZSLeWLNgg2SPu73gyNjQgLSBjb3JlIDE2NSByMzIyMyAwNDgwY2IwIC0gSC4yNjQvTVBFRy00IEFWQyBjb2RlYyAtIENvcHlsZWZ0IDIwMDMtMjAyNSAtIGh0dHA6Ly93d3cudmlkZW9sYW4ub3JnL3gyNjQuaHRtbCAtIG9wdGlvbnM6IGNhYmFjPTAgcmVmPTEgZGVibG9jaz0wOjA6MCBhbmFseXNlPTA6MCBtZT1kaWEgc3VibWU9MCBwc3k9MSBwc3lfcmQ9MS4wMDowLjAwIG1peGVkX3JlZj0wIG1lX3JhbmdlPTE2IGNocm9tYV9tZT0xIHRyZWxsaXM9MCA4eDhkY3Q9MCBjcW09MCBkZWFkem9uZT0yMSwxMSBmYXN0X3Bza2lwPTEgY2hyb21hX3FwX29mZnNldD0wIHRocmVhZHM9NSBsb29rYWhlYWRfdGhyZWFkcz01IHNsaWNlZF90aHJlYWRzPTEgc2xpY2VzPTUgbnI9MCBkZWNpbWF0ZT0xIGludGVybGFjZWQ9MCBibHVyYXlfY29tcGF0PTAgY29uc3RyYWluZWRfaW50cmE9MCBiZnJhbWVzPTAgd2VpZ2h0cD0wIGtleWludD02IGtleWludF9taW49MSBzY2VuZWN1dD0wIGludHJhX3JlZnJlc2g9MCByYz1jcmYgbWJ0cmVlPTAgY3JmPTIzLjAgcWNvbXA9MC42MCBxcG1pbj0wIHFwbWF4PTY5IHFwc3RlcD00IGlwX3JhdGlvPTEuNDAgYXE9MACAAAAAsmWIhDoRigACFZHAAEDyOAAIecnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAACWZQGSIhDoRigACFZHAAEDyOAAIecnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAAAtGUAtIiEOhGKAAIVkcAAQPI4AAh5ycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJ11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111114AAAAJZlAEYiIQ6EYoAAhWRwABA8jgACHnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXgAAAC0ZQBaIiEOhGKAAIVkcAAQPI4AAh5ycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJ11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111114AAAAAgnwAAAAB0GaID6AZMAAAAAIQQGSaID6AUMAAAAJQQC0miA+gGTAAAAACUEARiaID6AUMAAAAAlBAFomiA+gGTAAAAACCfAAAAAHQZpAPoBkwAAAAAhBAZJpAPoBQwAAAAlBALSaQD6AZMAAAAAJQQBGJpAPoBQwAAAACUEAWiaQD6AZMAAAAAIJ8AAAAAdBmmAQoBkwAAAACUEBkmmAQoBQwAAAAAlBALSaYBCgGTAAAAAJQQBGJpgEKAUMAAAACUEAWiaYBCgGTAAAAAIJ8AAAAAdBmoAQoBkwAAAACUEBkmoAQoBQwAAAAAlBALSagBCgGTAAAAAJQQBGJqAEKAUMAAAACUEAWiagBCgGTAAAAAIJ8AAAAAdBmqAQoBkwAAAACUEBkmqAQoBQwAAAAAlBALSaoBCgGTAAAAAJQQBGJqgEKAUMAAAACUEAWiaoBCgGTAAAAAIJ8AAAABlnQsAW2gKAv+XARAAAAwAEAAADADI8WLqAAAAABGjOD8gAAACzZYiCAQoRigACdhHAAEZSOAAKeMnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAACXZQGSIggEKEYoAAnYRwABGUjgACnjJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXgAAAALVlALSIggEKEYoAAnYRwABGUjgACnjJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXgAAAAl2UARiIggEKEYoAAnYRwABGUjgACnjJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXgAAAC1ZQBaIiCAQoRigACdhHAAEZSOAAKeMnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJycnJyddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddeAAAAAIJ8AAAAAdBmiA+gGTAAAAACEEBkmiA+gFDAAAACUEAtJogPoBkwAAAAAlBAEYmiA+gFDAAAAAJQQBaJogPoBkwAAAAAgnwAAAAB0GaQD6AZMAAAAAIQQGSaQD6AUMAAAAJQQC0mkA+gGTAAAAACUEARiaQD6AUMAAAAAlBAFomkA+gGTA="""

def _mp4_box(tag: bytes, payload: bytes) -> bytes:
    return struct.pack(">I4s", len(payload) + 8, tag) + payload

def build_fixture_mp4() -> bytes:
    """Build a fast-start MP4 whose playable media is complete before the held padding tail."""
    base = __import__("base64").b64decode(MP4_BASE64)
    if base[4:8] != b"ftyp" or b"moov" not in base[:2048] or b"mdat" not in base[:4096]:
        raise RuntimeError("embedded fixture must be a fast-start MP4")
    target = FAST_PREFIX_BYTES + 2 * 1024 * 1024
    free_size = target - len(base)
    if free_size < 8:
        raise RuntimeError("fixture padding target is too small")
    return base + _mp4_box(b"free", b"\0" * (free_size - 8))


@dataclass(frozen=True)
class FixtureRecord:
    sequence: int
    path: str
    role: str
    status: int
    accepted: bool
    headers: dict[str, str]

    def to_json(self) -> dict[str, Any]:
        return {
            "sequence": self.sequence,
            "path": self.path,
            "role": self.role,
            "status": self.status,
            "accepted": self.accepted,
            "headers": self.headers,
        }


class _FixtureHTTPServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], fixture: "Function0008Fixture") -> None:
        self.fixture = fixture
        super().__init__(address, _FixtureHandler)


class _FixtureHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, _format: str, *args: Any) -> None:
        return

    @property
    def fixture(self) -> "Function0008Fixture":
        return self.server.fixture  # type: ignore[attr-defined]

    def _json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def _small(self, status: int, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = self.path.split("?", 1)[0]
        if path == "/manifest.json":
            self.fixture.record(path, "manifest", 200, True, self.headers)
            self._json(200, self.fixture.manifest())
            return
        if path == f"/stream/movie/{VIDEO_ID}.json":
            self.fixture.record(path, "stream", 200, True, self.headers)
            self._json(200, self.fixture.stream_payload())
            return
        if path == "/video.mp4":
            self._serve_media()
            return
        self.fixture.record(path, "unknown", 404, False, self.headers)
        self._small(404, b"not found")

    def _serve_media(self) -> None:
        headers = self.fixture.selected_headers(self.headers)
        accepted = self.fixture.headers_are_valid(headers)
        role = self.fixture.classify_media_request(accepted)
        if not accepted:
            self.fixture.record("/video.mp4", role, 403, False, self.headers)
            self._small(403, b"function-0008 header contract rejected request")
            return

        body = self.fixture.media
        start, end, partial = self.fixture.parse_range(headers.get("Range", ""), len(body))
        if start is None or end is None:
            self.fixture.record("/video.mp4", role, 416, False, self.headers)
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{len(body)}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        self.fixture.record("/video.mp4", role, 206 if partial else 200, True, self.headers)
        self.send_response(206 if partial else 200)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(body)}")
        self.send_header("Connection", "close")
        self.end_headers()

        try:
            if role == "download" and start == 0 and self.fixture.hold_download:
                prefix_end = min(end + 1, FAST_PREFIX_BYTES)
                sent = 0
                while sent < prefix_end:
                    chunk_end = min(prefix_end, sent + DOWNLOAD_CHUNK_BYTES)
                    self.wfile.write(body[sent:chunk_end])
                    self.wfile.flush()
                    sent = chunk_end
                    if sent < prefix_end:
                        time.sleep(DOWNLOAD_CHUNK_DELAY_SECONDS)
                self.fixture.download_prefix_sent.set()
                if not self.fixture.release_download.wait(timeout=180):
                    return
                if prefix_end <= end:
                    self.wfile.write(body[prefix_end:end + 1])
            else:
                self.wfile.write(body[start:end + 1])
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


class Function0008Fixture:
    def __init__(self, *, hold_download: bool = False) -> None:
        self.media = build_fixture_mp4()
        self.hold_download = hold_download
        self.negative_control = False
        if len(self.media) <= FAST_PREFIX_BYTES:
            raise RuntimeError("fixture media must cross the disk-first threshold")
        if self.media[4:8] != b"ftyp" or b"moov" not in self.media[:FAST_PREFIX_BYTES]:
            raise RuntimeError("fixture MP4 must expose fast-start metadata before the held frontier")
        self.release_download = threading.Event()
        self.download_prefix_sent = threading.Event()
        self.playback_request_seen = threading.Event()
        self._lock = threading.Lock()
        self._records: list[FixtureRecord] = []
        self._sequence = 0
        self._accepted_media_count = 0
        self._server = _FixtureHTTPServer(("127.0.0.1", 0), self)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return int(self._server.server_address[1])

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    @property
    def video_url(self) -> str:
        return self.base_url + "/video.mp4"

    def manifest(self) -> dict[str, Any]:
        return {
            "id": "org.preflight.function0008.fixture",
            "version": "1.0.0",
            "name": "Function 0008 Loopback",
            "description": "Deterministic Direct HTTP source for runtime proof.",
            "resources": ["stream"],
            "types": ["movie"],
            "idPrefixes": ["function0008-"],
        }

    def stream_payload(self) -> dict[str, Any]:
        return {
            "streams": [{
                "name": "Function 0008 Direct 720p",
                "title": "Direct HTTP header provenance fixture",
                "url": self.video_url,
                "behaviorHints": {
                    "proxyHeaders": {
                        "request": dict(EXPECTED_REQUEST_HEADERS),
                        "response": {NEGATIVE_HEADER: NEGATIVE_VALUE},
                    }
                },
            }]
        }

    @staticmethod
    def selected_headers(headers: Any) -> dict[str, str]:
        names = [
            "User-Agent", "Range", "Referer", "Origin",
            "X-Function-0008", NEGATIVE_HEADER,
        ]
        return {name: str(headers.get(name, "")) for name in names}

    def headers_are_valid(self, headers: dict[str, str]) -> bool:
        for name, expected in EXPECTED_REQUEST_HEADERS.items():
            if headers.get(name, "") != expected:
                return False
        sentinel = headers.get(NEGATIVE_HEADER, "")
        if self.negative_control:
            return sentinel == NEGATIVE_VALUE
        return not sentinel

    def set_negative_control(self, enabled: bool) -> None:
        with self._lock:
            self.negative_control = bool(enabled)

    def classify_media_request(self, accepted: bool) -> str:
        if not accepted:
            return "rejected"
        with self._lock:
            self._accepted_media_count += 1
            role = "download" if self._accepted_media_count == 1 else "playback"
            if role == "playback":
                self.playback_request_seen.set()
            return role

    @staticmethod
    def parse_range(value: str, size: int) -> tuple[int | None, int | None, bool]:
        if not value:
            return 0, size - 1, False
        if not value.startswith("bytes=") or "," in value:
            return None, None, True
        first, _, last = value[6:].partition("-")
        try:
            start = int(first) if first else 0
            end = int(last) if last else size - 1
        except ValueError:
            return None, None, True
        if start < 0 or start >= size or end < start:
            return None, None, True
        return start, min(end, size - 1), True

    def record(self, path: str, role: str, status: int, accepted: bool,
               headers: Any) -> None:
        with self._lock:
            self._sequence += 1
            self._records.append(FixtureRecord(
                self._sequence, path, role, status, accepted,
                self.selected_headers(headers),
            ))

    def records(self) -> list[dict[str, Any]]:
        with self._lock:
            return [record.to_json() for record in self._records]

    def reset_runtime_records(self) -> None:
        with self._lock:
            self._records.clear()
            self._sequence = 0
            self._accepted_media_count = 0
        self.release_download.clear()
        self.download_prefix_sent.clear()
        self.playback_request_seen.clear()

    def start(self) -> "Function0008Fixture":
        self._thread.start()
        return self

    def close(self) -> None:
        self.release_download.set()
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=5)

    def __enter__(self) -> "Function0008Fixture":
        return self.start()

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        self.close()


def _urlopen(url: str, headers: dict[str, str] | None = None) -> tuple[int, bytes]:
    request = urllib.request.Request(url, headers=headers or {})
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return int(response.status), response.read()
    except urllib.error.HTTPError as error:
        return int(error.code), error.read()


def self_test() -> None:
    with Function0008Fixture() as fixture:
        status, manifest_body = _urlopen(fixture.base_url + "/manifest.json")
        assert status == 200 and json.loads(manifest_body)["id"] == "org.preflight.function0008.fixture"
        status, stream_body = _urlopen(fixture.base_url + f"/stream/movie/{VIDEO_ID}.json")
        assert status == 200 and json.loads(stream_body)["streams"][0]["url"] == fixture.video_url

        good = dict(EXPECTED_REQUEST_HEADERS)
        good["Range"] = "bytes=0-255"
        status, body = _urlopen(fixture.video_url, good)
        assert status == 206 and body[4:8] == b"ftyp"

        missing = dict(EXPECTED_REQUEST_HEADERS)
        missing.pop("Origin")
        status, _ = _urlopen(fixture.video_url, missing)
        assert status == 403

        fixture.set_negative_control(True)
        status, _ = _urlopen(fixture.video_url, dict(EXPECTED_REQUEST_HEADERS))
        assert status == 403
        fixture.set_negative_control(False)
        restored = dict(EXPECTED_REQUEST_HEADERS)
        restored["Range"] = "bytes=0-31"
        status, restored_body = _urlopen(fixture.video_url, restored)
        assert status == 206 and restored_body[4:8] == b"ftyp"

        records = fixture.records()
        assert any(row["accepted"] and row["status"] in (200, 206) for row in records)
        assert any(row["status"] == 403 for row in records)
        assert fixture.media[4:8] == b"ftyp"
        assert b"moov" in fixture.media[:FAST_PREFIX_BYTES]
        print(
            f"FUNCTION0008_FIXTURE_OK port={fixture.port} bytes={len(fixture.media)} "
            f"moov={fixture.media.find(b'moov')} records={len(records)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Function 0008 deterministic loopback fixture")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("choose --self-test")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

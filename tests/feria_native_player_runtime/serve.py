"""Loopback-only fixtures for Smoke.qml; media is generated outside the checkout."""
import argparse
import re
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("fixtures", type=Path)
parser.add_argument("--port", type=int, default=0)
args = parser.parse_args()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.respond(False)

    def do_HEAD(self):
        self.respond(True)

    def respond(self, head):
        name = self.path.split("?")[0].lstrip("/")
        if name == "website":
            data = b'<html><title>Feria fixture</title><video controls muted src="/h264-aac.mp4"></video></html>'
            mime = "text/html"
        elif name in ("h264-aac.mp4", "ffv1-pcm.mkv"):
            data = (args.fixtures / name).read_bytes()
            mime = "video/mp4" if name.endswith(".mp4") else "video/x-matroska"
        else:
            self.send_error(404)
            return
        match = re.fullmatch(r"bytes=(\d+)-(\d*)", self.headers.get("Range", ""))
        start, end = 0, len(data) - 1
        if match:
            start = int(match[1])
            end = min(int(match[2]) if match[2] else end, end)
            if start > end:
                self.send_error(416)
                return
        self.send_response(206 if match else 200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        if match:
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(data)}")
        self.end_headers()
        if not head:
            self.wfile.write(data[start:end + 1])


server = HTTPServer(("127.0.0.1", args.port), Handler)
print(f"http://127.0.0.1:{server.server_port}", flush=True)
server.serve_forever()

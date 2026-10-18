"""Real loopback redirects must never receive the admin bearer."""
import importlib.util
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import threading
import unittest


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).resolve().parents[1] / "tools" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


publisher = load_tool("redirect_publisher", "publish.py")
rotation = load_tool("redirect_rotation", "rotate-admin-token.py")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        self.server.received.append(self.headers.get("Authorization"))
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        code = self.server.redirect_code
        self.send_response(code or 200)
        if code:
            self.send_header("Location", self.server.destination)
        self.end_headers()
        if not code:
            self.wfile.write(b'{"ok":true,"tables":[],"budget":[]}')

    do_POST = do_GET


@contextmanager
def serve():
    server = HTTPServer(("127.0.0.1", 0), Handler)
    server.received = []
    server.redirect_code = 0
    thread = threading.Thread(target=server.serve_forever,
                              kwargs={"poll_interval": 0.01}, daemon=True)
    thread.start()
    try:
        yield server, "http://127.0.0.1:" + str(server.server_port)
    finally:
        server.shutdown()
        thread.join(timeout=2)
        server.server_close()


class AdminRedirectTests(unittest.TestCase):
    def test_publisher_rejects_redirect_before_bearer_leaves_origin(self):
        with serve() as (sink, destination), serve() as (origin, base):
            origin.destination = destination + "/unexpected"
            client = publisher.Client({"baseUrl": base, "adminToken": "fixture-token"})
            for method in ("GET", "POST"):
                for code in (301, 302, 303, 307, 308):
                    with self.subTest(method=method, code=code):
                        origin.redirect_code = code
                        sink.received.clear()
                        rejected = False
                        try:
                            client.request(method, "/admin/status", {} if method == "POST" else None)
                        except publisher.PublishError:
                            rejected = True
                        self.assertEqual(sink.received, [])
                        self.assertTrue(rejected)
            origin.redirect_code = 0
            self.assertTrue(client.request("GET", "/admin/status")["ok"])
            self.assertTrue(all(value == "Bearer fixture-token" for value in origin.received))

    def test_rotation_reports_redirect_without_sending_bearer_to_destination(self):
        with serve() as (sink, destination), serve() as (origin, base):
            origin.destination = destination + "/unexpected"
            for code in (301, 302, 303, 307, 308):
                with self.subTest(code=code):
                    origin.redirect_code = code
                    sink.received.clear()
                    status = rotation.status_code(base, "fixture-token")
                    self.assertEqual(sink.received, [])
                    self.assertEqual(status, code)
            origin.redirect_code = 0
            self.assertEqual(rotation.status_code(base, "fixture-token"), 200)


if __name__ == "__main__":
    unittest.main()

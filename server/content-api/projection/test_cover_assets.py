import hashlib
from contextlib import closing
import io
import os
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

from PIL import Image
import cover_assets as covers


class CoverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        image = io.BytesIO()
        Image.new("RGB", (600, 900), "blue").save(image, "PNG")
        cls.png = image.getvalue()
        cls.hits = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                cls.hits.append(self.path)
                if self.path == "/429":
                    self.send_response(429)
                    self.send_header("Retry-After", "300")
                    self.end_headers()
                    return
                if self.path in {"/redirect", "/private"}:
                    self.send_response(302)
                    self.send_header("Location", "/image" if self.path == "/redirect" else "http://169.254.169.254/secret")
                    self.end_headers()
                    return
                if self.path == "/html":
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html")
                    self.end_headers()
                    self.wfile.write(b"challenge")
                    return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(cls.png)))
                self.end_headers()
                self.wfile.write(cls.png)
            def log_message(self, *args):
                pass
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.manifest = self.root / "manifest.json"
        self.hits.clear()

    def tearDown(self):
        self.temp.cleanup()

    def fetch(self, paths, **kwargs):
        return covers.fetch_assets({self.base + path: [{"kind": "test"}] for path in paths}, self.root, self.manifest, "https://covers.example.org", allowed_hosts={"127.0.0.1"}, allow_loopback=True, interval=0, **kwargs)

    def test_resume_transform_and_content_hash(self):
        manifest = self.fetch(["/image"])
        entry = manifest["assets"][self.base + "/image"]
        self.assertEqual((entry["width"], entry["height"]), (261, 392))
        self.assertEqual(entry["sha256"], hashlib.sha256((self.root / entry["key"]).read_bytes()).hexdigest())
        self.fetch(["/image"])
        self.assertEqual(self.hits, ["/image"])
        verified = covers.verified_urls(manifest, self.root)
        original = self.base + "/image"
        payload = {"rows": [{"cover": original, "source_cover": "https://source.example/missing", "covers": [original, "https://source.example/missing"], "coverCandidates": ["https://fallback.example/image"]}]}
        transformed = covers.transform_publication(payload, verified)
        self.assertEqual(transformed["rows"][0]["cover"], entry["owned_url"])
        self.assertEqual(transformed["rows"][0]["coverCandidates"], ["https://fallback.example/image", original])
        self.assertEqual(transformed["rows"][0]["source_cover"], "https://source.example/missing")
        self.assertEqual(payload["rows"][0]["cover"], original)
        self.assertEqual(covers.transform_publication(transformed, verified), transformed)
        (self.root / entry["key"]).write_bytes(b"x" * entry["bytes"])
        with self.assertRaises(covers.SafetyError):
            covers.verified_urls(manifest, self.root)
        with self.assertRaises(covers.SafetyError):
            self.fetch(["/image"])

    def test_stop_and_retry_after_recorded(self):
        manifest = self.fetch(["/429", "/image"])
        self.assertEqual(self.hits, ["/429"])
        entry = manifest["assets"][self.base + "/429"]
        self.assertEqual(entry["status"], "stopped")
        self.assertIn("300", entry["error"])
        self.fetch(["/429", "/image"])
        self.assertEqual(self.hits, ["/429"])
        self.assertEqual(covers.verified_urls(manifest, self.root), {})

    def test_html_challenge_stops(self):
        self.fetch(["/html", "/image"])
        self.assertEqual(self.hits, ["/html"])

    def test_caps_and_redirect(self):
        manifest = self.fetch(["/redirect"])
        self.assertEqual(self.hits, ["/redirect", "/image"])
        self.assertEqual(len(covers.verified_urls(manifest, self.root)), 1)
        self.manifest.unlink()
        result = self.fetch(["/image"], max_bytes=10)
        self.assertEqual(result["assets"][self.base + "/image"]["status"], "failed")
        with self.assertRaises(covers.SafetyError):
            covers.encode_webp(self.png, max_pixels=10)
        small = io.BytesIO()
        Image.new("RGB", (12, 15)).save(small, "PNG")
        self.assertEqual(covers.encode_webp(small.getvalue())[1], (12, 15))

    def test_private_redirect_and_loopback_rejected(self):
        with self.assertRaises(covers.SafetyError):
            covers.download(self.base + "/image", allowed_hosts={"127.0.0.1"})
        with self.assertRaises(covers.SafetyError):
            covers.download(self.base + "/private", allowed_hosts={"127.0.0.1", "169.254.169.254"}, allow_loopback=True)
        self.assertEqual(self.hits, ["/private"])
        with self.assertRaises(covers.SafetyError):
            covers.download(self.base + "/image", allowed_hosts={"other.example"}, allow_loopback=True)
        for url in ("file:///tmp/a", "http://user:secret@example.org/a", "http://example.org/a#fragment", "http://example.org/\n"):
            with self.assertRaises(covers.SafetyError):
                covers.parse_url(url)

    def test_mixed_dns_and_pinned_ip(self):
        public = (2, 1, 6, "", ("8.8.8.8", 80))
        private = (2, 1, 6, "", ("127.0.0.1", 80))
        with patch.object(covers.socket, "getaddrinfo", return_value=[public, private]):
            with self.assertRaises(covers.SafetyError):
                covers.resolve_url("http://public.example/image")
        # A hostname that changes DNS later cannot change the pinned connect address.
        with patch.object(covers.socket, "getaddrinfo", return_value=[(2, 1, 6, "", ("127.0.0.1", self.server.server_port))]) as resolver:
            body, _ = covers.download("http://fixture.example/image", allowed_hosts={"fixture.example"}, allow_loopback=True)
            self.assertEqual(body, self.png)
            self.assertEqual(resolver.call_count, 1)

    def test_budgets_and_explicit_revalidation(self):
        manifest = self.fetch(["/image", "/image2"], max_downloads=1)
        self.assertEqual(self.hits, ["/image"])
        self.fetch(["/image"], revalidate=True)
        self.assertEqual(self.hits, ["/image", "/image"])
        self.fetch(["/image"], ttl=0)
        self.assertEqual(len(self.hits), 3)
        self.manifest.unlink()
        stopped = self.fetch(["/image"], max_stored_bytes=1)
        # Existing same-content object may be reused within the object budget.
        self.assertEqual(stopped["assets"][self.base + "/image"]["status"], "complete")

    def test_dns_deadline_and_storage_stop(self):
        def slow_dns(*args, **kwargs):
            time.sleep(0.1)
            return []
        with patch.object(covers.socket, "getaddrinfo", side_effect=slow_dns):
            with self.assertRaises(covers.StopFetch):
                covers.resolve_url("http://public.example/image", timeout=0.01)
            time.sleep(0.12)
        result = self.fetch(["/image", "/image2"], max_stored_bytes=1)
        self.assertEqual(self.hits, ["/image"])
        self.assertEqual(result["assets"][self.base + "/image"]["status"], "stopped")
        self.assertEqual(list((self.root / "covers").glob("*")), [])

    def test_cli_refuses_overwrite_and_keeps_source(self):
        source = self.root / "source.json"
        source.write_text(json.dumps([self.base + "/image"]))
        before = source.read_bytes()
        script = str(Path(covers.__file__).resolve())
        command = [sys.executable, script, "fetch", "--input-json", str(source), "--manifest", str(source), "--root", str(self.root), "--origin", "https://covers.example.org", "--allowed-host", "127.0.0.1"]
        result = subprocess.run(command, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Refusing to overwrite input", result.stderr)
        self.assertEqual(source.read_bytes(), before)
        manifest = self.fetch(["/image"])
        publication = self.root / "publication.json"
        publication.write_text(json.dumps({"cover": self.base + "/image"}))
        output = self.root / "output.json"
        command = [sys.executable, script, "transform", "--input", str(publication), "--output", str(output), "--manifest", str(self.manifest), "--root", str(self.root)]
        self.assertEqual(subprocess.run(command, capture_output=True, timeout=5).returncode, 0)
        self.assertEqual(json.loads(output.read_text())["cover"], manifest["assets"][self.base + "/image"]["owned_url"])
        self.assertNotEqual(subprocess.run(command, capture_output=True, timeout=5).returncode, 0)

    def test_cli_inventory_and_fetch_success(self):
        script = str(Path(covers.__file__).resolve())
        source = self.root / "source.json"
        source.write_text(json.dumps([self.base + "/image", self.base + "/image"]))
        before = source.read_bytes()
        db = self.root / "source.sqlite"
        with closing(sqlite3.connect(db)) as connection:
            connection.execute("CREATE TABLE series(cover TEXT)")
            connection.execute("INSERT INTO series VALUES (?)", (self.base + "/image",))
            connection.commit()
        db_before = db.read_bytes()
        result = subprocess.run([sys.executable, script, "inventory", "--input-json", str(source), "--source-sqlite", str(db)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        inventory = json.loads(result.stdout)
        self.assertEqual(inventory["unique_urls"], 1)
        self.assertEqual(len(inventory["urls"][self.base + "/image"]), 3)
        self.assertEqual(self.hits, [])
        command = [sys.executable, script, "fetch", "--input-json", str(source), "--manifest", str(self.manifest), "--root", str(self.root), "--origin", "https://covers.example.org", "--allowed-host", "127.0.0.1", "--allow-loopback-test", "--interval", "0"]
        for _ in range(2):
            result = subprocess.run(command, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(json.loads(result.stdout)["complete"], 1)
        self.assertEqual(self.hits, ["/image"])
        self.assertEqual(len(covers.verified_urls(json.loads(self.manifest.read_text()), self.root)), 1)
        exported = subprocess.run([sys.executable, script, "export-map", "--manifest", str(self.manifest), "--root", str(self.root)], capture_output=True, timeout=10)
        self.assertEqual(exported.returncode, 0, exported.stderr.decode())
        self.assertEqual(len(json.loads(exported.stdout)["urls"]), 1)
        self.assertEqual(source.read_bytes(), before)
        self.assertEqual(db.read_bytes(), db_before)

    def test_owned_origin_rejects_local_normalized_aliases(self):
        invalid = ["https://localhost", "https://x.localhost", "https://printer.local", "https://127.1", "https://2130706433", "https://0x7f000001", "https://0177.0.0.1", "https://%31%32%37.0.0.1", "https://[::1]", "https://[fc00::1]", "https://[fe80::1]", "https://[::ffff:127.0.0.1]", "https://192.168.1.2", "https://100.64.0.1"]
        for origin in invalid:
            with self.subTest(origin=origin), self.assertRaises(covers.SafetyError):
                covers.origin_url(origin)
        self.assertEqual(covers.origin_url("https://covers.example.org"), "https://covers.example.org")
        self.assertEqual(covers.origin_url("https://8.8.8.8"), "https://8.8.8.8")

    def test_verify_checks_dimensions_before_loading_pixels(self):
        data = b"synthetic hashed file"
        digest = hashlib.sha256(data).hexdigest()
        key = f"covers/{digest}.webp"
        path = self.root / key
        path.parent.mkdir()
        path.write_bytes(data)
        entry = dict(status="complete", sha256=digest, key=key, owned_url="https://covers.example.org/"+key, bytes=len(data), width=9999, height=9999)
        with patch.object(covers.Image, "open") as opened:
            image = opened.return_value.__enter__.return_value
            image.format = "WEBP"
            image.width = image.height = 9999
            image.size = (9999, 9999)
            with self.assertRaises(covers.SafetyError):
                covers.verify_entry(entry, self.root, "https://covers.example.org")
            image.load.assert_not_called()

    def test_python_fallback_order_is_stable_across_hash_seeds(self):
        script = "import cover_assets,json; print(json.dumps(cover_assets.transform_publication({'cover':'c','poster':'p','source_cover':'s'},{'c':'owned-c','p':'owned-p','s':'owned-s'})['coverCandidates']))"
        for seed in ("1", "2", "3"):
            result = subprocess.run([sys.executable, "-c", script], cwd=str(Path(covers.__file__).parent), env=dict(os.environ, PYTHONHASHSEED=seed), capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(json.loads(result.stdout), ["c", "p", "s"])

    def test_readonly_deduplicated_inventory(self):
        db = self.root / "source.sqlite"
        with closing(sqlite3.connect(db)) as connection:
            connection.executescript("CREATE TABLE series(cover TEXT); CREATE TABLE curated_source(source_cover TEXT);")
            connection.execute("INSERT INTO series VALUES (?)", (self.base + "/image",))
            connection.execute("INSERT INTO curated_source VALUES (?)", (self.base + "/image",))
            connection.commit()
        source = self.root / "urls.json"
        source.write_text(json.dumps([self.base + "/image", self.base + "/image2"]))
        before = db.read_bytes()
        inventory = covers.inventory(source, db)
        self.assertEqual(len(inventory), 2)
        self.assertEqual(len(inventory[self.base + "/image"]), 3)
        self.assertEqual(db.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

import http.server
import json
import os
from pathlib import Path
import shutil
import ssl
import threading
import time
import urllib.request

from windows_update_rehearsal import ORIGIN, PORT, digest, openssl, require

PACKAGE = "Colosseum-1.5.1-setup.exe"


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def make_tls(work):
    keys = work / "keys"
    keys.mkdir()
    ca, leaf = keys / "ca.pem", keys / "server.pem"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=Colosseum diagnostic loopback CA", "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", keys / "ca-key.pem", "-out", ca)
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=127.0.0.1",
            "-keyout", keys / "server-key.pem", "-out", keys / "server.csr")
    ext = keys / "server-ext.cnf"
    ext.write_text("subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n"
                   "basicConstraints=critical,CA:FALSE\n", encoding="ascii")
    openssl("x509", "-req", "-in", keys / "server.csr", "-CA", ca, "-CAkey", keys / "ca-key.pem",
            "-CAcreateserial", "-days", "2", "-extfile", ext, "-out", leaf)
    openssl("verify", "-CAfile", ca, "-verify_ip", "127.0.0.1", leaf)
    os.environ["COLOSSEUM_WINDOWS_REHEARSAL_CA"] = str(ca)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(leaf, keys / "server-key.pem")
    return context


class Feed:
    def __init__(self, work, evidence):
        self.root = work / "https"
        self.downloads = self.root / "updates/downloads/v1.5.1"
        self.downloads.mkdir(parents=True)
        self.original_signature = (evidence / "colosseum-update-v1.json.sig").read_bytes()
        self.manifest = Path(shutil.copy2(evidence / "colosseum-update-v1.json", self.downloads))
        self.signature = self.downloads / "colosseum-update-v1.json.sig"
        self.installer = Path(shutil.copy2(work / "fixtures" / PACKAGE, self.downloads))
        self.requests = []
        self.lock = threading.Lock()

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(inner, *args, **kwargs):
                super().__init__(*args, directory=self.root, **kwargs)

            def do_GET(inner):
                with self.lock:
                    self.requests.append({"time": time.time(), "path": inner.path,
                                          "user_agent": inner.headers.get("User-Agent", "")})
                super().do_GET()

        tls = make_tls(work)

        class Server(http.server.ThreadingHTTPServer):
            def get_request(inner):
                conn, peer = super().get_request()
                try:
                    conn.settimeout(5)
                    secured = tls.wrap_socket(conn, server_side=True)
                    secured.settimeout(20)
                    return secured, peer
                except OSError as error:
                    conn.close()
                    with self.lock:
                        self.requests.append({"time": time.time(), "tls_error": str(error)})
                    print(f"Loopback TLS accept failed: {error}", flush=True)
                    raise

        self.server = Server(("127.0.0.1", PORT), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def configure(self):
        self.signature.write_bytes(self.original_signature)
        assets = [
            {"name": path.name, "size": path.stat().st_size, "digest": "sha256:" + digest(path),
             "browser_download_url": ORIGIN + "/" + path.relative_to(self.root).as_posix()}
            for path in (self.manifest, self.signature, self.installer)
        ]
        write_json(self.root / "updates/stable.json", {"tag_name": "v1.5.1", "draft": False,
                                                      "prerelease": False, "assets": assets})

    def verify_origin(self):
        tls = ssl.create_default_context(cafile=os.environ["COLOSSEUM_WINDOWS_REHEARSAL_CA"])
        with urllib.request.urlopen(ORIGIN + "/updates/stable.json", context=tls, timeout=8) as reply:
            require(json.load(reply)["tag_name"] == "v1.5.1", "TLS fixture self-check failed")
        print("Independent HTTPS client verified the loopback origin", flush=True)

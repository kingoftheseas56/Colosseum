"""Local TLS/origin checks using the real Feria wrapper and a pinned runtime.

Requires Python cryptography and psutil. Trust is confined to a disposable Qt
profile; no OS certificate installation and no certificate-bypass flags.
Each invocation needs a new --out directory and preserves native root exits.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import os
from pathlib import Path
import ssl
import subprocess
import threading
import time

import psutil
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def digest(path):
    with open(path, "rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def certificate(name, out, issuer=None, expired=False, wrong_host=False):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = datetime.now(timezone.utc)
    ca = issuer is None
    builder = (x509.CertificateBuilder().subject_name(subject)
               .issuer_name(subject if ca else issuer[0].subject).public_key(key.public_key())
               .serial_number(x509.random_serial_number())
               .not_valid_before(now - timedelta(days=3))
               .not_valid_after(now + timedelta(days=-1 if expired else 7))
               .add_extension(x509.BasicConstraints(ca=ca, path_length=0 if ca else None), True))
    if not ca:
        san = (x509.DNSName("wrong.example.test") if wrong_host
               else x509.IPAddress(ipaddress.ip_address("127.0.0.1")))
        builder = builder.add_extension(x509.SubjectAlternativeName([san]), False)
        builder = builder.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False)
    cert = builder.sign(key if ca else issuer[1], hashes.SHA256())
    cert_path, key_path = out / (name + ".pem"), out / (name + ".key")
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                         serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return cert, key, cert_path, key_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--variant", choices=["stock", "codec"], required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[2]
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    pairing = next(p for p in manifest["pairings"] if p["variant"] == args.variant)
    runtime = Path(pairing["runtimeRoot"])
    binary_root = runtime / "bin" if args.variant == "stock" else runtime
    plugins = runtime / "plugins" if args.variant == "stock" else runtime
    executable = binary_root / "feria_foundation_runtime.exe"
    verified = {}
    for item in pairing["selectedBinaries"]:
        path = Path(item["path"])
        if digest(path).lower() != item["sha256"].lower():
            raise RuntimeError("Runtime hash mismatch: " + str(path))
        verified[str(path.resolve()).lower()] = item["sha256"].lower()
    overlap = [p.info for p in psutil.process_iter(["pid", "name", "exe"])
               if p.info["name"] in ("feria_foundation_runtime.exe", "QtWebEngineProcess.exe")]
    if overlap:
        (out / "blocked.json").write_text(json.dumps(overlap, indent=2))
        raise RuntimeError("Existing browser test processes; no launch")
    authority = certificate("fixture-ca", out)
    foreign_authority = certificate("untrusted-ca", out)
    config = {"caFile": authority[2].as_posix(), "profilePath": (out / "profile").as_posix()}
    server_hits = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def do_GET(self):
            server_hits.append({"port": self.server.server_port, "path": self.path})
            if self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", config["expired"] + "/page")
                self.end_headers()
                return
            body = (b"fixture-secret" if self.path in ("/secret", "/cors") else
                    b"<!doctype html><title>Security fixture</title><p id='fixture'>accepted</p>"
                    b"<script>window.inlineRan=true</script>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            if self.path == "/cors":
                self.send_header("Access-Control-Allow-Origin", "*")
            if self.path == "/csp":
                self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'none'")
            self.end_headers()
            self.wfile.write(body)

    servers = []
    for name in ("valid", "expired", "wrongHost", "untrusted", "crossOrigin"):
        cert = certificate(name, out, foreign_authority if name == "untrusted" else authority,
                           expired=name == "expired", wrong_host=name == "wrongHost")
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert[2], cert[3])
        server.socket = context.wrap_socket(server.socket, server_side=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        servers.append(server)
        config[name] = "https://127.0.0.1:" + str(server.server_port)
    secret = out / "local-secret.txt"
    secret.write_text("must-not-reach-page")
    config["secretFile"] = secret.as_uri()
    qml = out / "qml"
    qml.mkdir()
    template = Path(__file__).with_name("tst_transport.qml.in")
    generated = template.read_text().replace("@FERIA_IMPORT@", json.dumps((repo / "qml/feria").as_uri()))
    (qml / "tst_transport.qml").write_text(generated)
    (qml / "FixtureConfig.js").write_text(".pragma library\nvar values = " + json.dumps(config) + ";\n")
    env = dict(os.environ)
    for name in ("QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_DISABLE_SANDBOX",
                 "QTWEBENGINE_REMOTE_DEBUGGING", "FERIA_SKIP_ORDERLY_SHUTDOWN", "CHROME_DEVEL_SANDBOX"):
        env.pop(name, None)
    env.update(PATH=str(binary_root) + os.pathsep + env.get("PATH", ""),
               QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(plugins),
               QT_QPA_PLATFORM_PLUGIN_PATH=str(plugins / "platforms"),
               QML_IMPORT_PATH=str(runtime / "qml"), QML2_IMPORT_PATH=str(runtime / "qml"),
               QTWEBENGINEPROCESS_PATH=str(binary_root / "QtWebEngineProcess.exe"),
               QTWEBENGINE_RESOURCES_PATH=str(runtime / "resources"),
               QTWEBENGINE_LOCALES_PATH=str(runtime / "translations/qtwebengine_locales"),
               FERIA_FIXTURE_PORT="0", FERIA_FIXTURE_ROOT=str(out / "profiles"),
               QT_QUICK_CONTROLS_STYLE="Basic")
    command = [str(executable), "-input", str(qml), "-o", str(out / "qtest.txt") + ",txt"]
    source_hashes = {str(path.relative_to(repo)): digest(path) for path in
                     (repo / "qml/feria/FeriaQtWebEngine.qml", repo / "qml/feria/FeriaBrowserRequests.qml")}
    receipt = {"command": command, "runnerSha256": digest(executable), "verifiedModules": verified,
               "sourceBefore": source_hashes, "systemTrustChanged": False, "modulePaths": []}
    (out / "launch.json").write_text(json.dumps(receipt, indent=2))
    try:
        with (out / "stdout.log").open("wb") as stdout, (out / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=runtime, env=env, stdout=stdout, stderr=stderr)
            receipt["pid"] = process.pid
            start = time.monotonic()
            observed = set()
            children = {}
            while process.poll() is None and time.monotonic() - start < 150:
                try:
                    root = psutil.Process(process.pid)
                    for child in root.children(recursive=True):
                        children[child.pid] = {"pid": child.pid, "created": child.create_time(),
                                               "exe": str(Path(child.exe()).resolve()).lower()}
                    for item in root.memory_maps():
                        path = item.path.lower()
                        if "qt6webengine" in path or "qtwebenginequickplugin" in path:
                            observed.add(str(Path(item.path).resolve()).lower())
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
                time.sleep(0.5)
            receipt.update(nativeExit=process.poll(), elapsedSeconds=time.monotonic() - start,
                           modulePaths=sorted(observed), serverHits=server_hits,
                           observedChildren=list(children.values()))
            receipt["sourceStable"] = all(digest(repo / path) == value for path, value in source_hashes.items())
            receipt["modulesMatchManifest"] = bool(observed) and all(path in verified for path in observed)
            receipt["helpersMatchManifest"] = bool(children) and all(child["exe"] in verified for child in children.values())
            remaining = []
            for child in children.values():
                try:
                    current = psutil.Process(child["pid"])
                    if current.create_time() == child["created"]:
                        try:
                            current.wait(timeout=5)
                        except psutil.TimeoutExpired:
                            remaining.append(child)
                except psutil.NoSuchProcess:
                    pass
            receipt["remainingOwnedChildren"] = remaining
            (out / "result.json").write_text(json.dumps(receipt, indent=2))
            if process.poll() is None:
                raise RuntimeError("Native root still running; PID recorded, no retry or success claim")
            if (process.returncode or not receipt["sourceStable"] or not receipt["modulesMatchManifest"]
                    or not receipt["helpersMatchManifest"] or remaining):
                raise RuntimeError("Transport qualification failed; inspect preserved result and logs")
            print("Transport checks passed; native exit 0; manifest modules and source stable")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()

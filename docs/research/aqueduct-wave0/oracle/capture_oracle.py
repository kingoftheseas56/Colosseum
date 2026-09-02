#!/usr/bin/env python3
"""Capture normalized behavioral fixtures from the exact Stremio 4.20.17 oracle."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

from fixture_server import create_server

SERVER_SHA256 = "567a397bb11b788571bf1750fd05dd78927f97bec0c9ddeaa6d9cc1eccee3922"
RUNTIME_SHA256 = "8ad810919df76741a153dbf28180a84f7ab395ea3da2534374a10f0e6dca7e3b"
LAB_PORT = 11480
FIXTURE_PORT = 11580
PROD_PORT = 11470
SINTEL_HASH = "08ada5a7a6183aae1e09d831df6748d566095a10"
SINTEL_INDEX = 5
SCRIPT = Path(__file__).resolve()
REPO = SCRIPT.parents[4]
LAB_ROOT = REPO / "native" / "build-msvc" / "_aqueduct-wave0" / "oracle"
DEFAULT_SOURCE = Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Colosseum" / "native" / "build-msvc" / "stream_server"
STABLE_HEADERS = {
    "accept-ranges", "cache-control", "content-features.dlna.org", "content-length",
    "content-range", "content-type", "location", "transfermode.dlna.org",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_port_free(port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.25)
        if sock.connect_ex(("127.0.0.1", port)) == 0:
            raise RuntimeError(f"refusing to start: lab port {port} is already in use")


def wait_port_closed(port: int, timeout: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            if sock.connect_ex(("127.0.0.1", port)) != 0:
                return True
        time.sleep(0.1)
    return False

def remove_tree_with_retries(path: Path, attempts: int = 20, delay: float = 0.25) -> None:
    if not path.exists():
        return
    for attempt in range(attempts):
        try:
            shutil.rmtree(path)
            return
        except PermissionError:
            if attempt + 1 >= attempts:
                raise
            time.sleep(delay)


def terminate_process_tree(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
        )
    else:
        process.terminate()
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def normalize_json(value, base_url: str, app_path: Path):
    if isinstance(value, dict):
        result = {k: normalize_json(v, base_url, app_path) for k, v in value.items()}
        if "appPath" in result:
            result["appPath"] = "<APP_PATH>"
        if "cacheRoot" in result:
            result["cacheRoot"] = "<APP_PATH>"
        if result.get("id") == "remoteHttps" and isinstance(result.get("selections"), list):
            result["selections"] = [x for x in result["selections"] if x.get("val") == ""] + [
                {"name": "<NETWORK_INTERFACES>", "val": "<NETWORK_INTERFACES>"}
            ]
        return result
    if isinstance(value, list):
        return [normalize_json(item, base_url, app_path) for item in value]
    if isinstance(value, str):
        value = value.replace(str(app_path), "<APP_PATH>").replace(base_url, "<BASE_URL>")
        return re.sub(r"http://[^/]+:11480", "<ORACLE_BASE_URL>", value)
    return value


def normalize_location(value: str, base_url: str) -> str:
    return value.replace(urllib.parse.quote(base_url, safe=""), "<BASE_URL_ENCODED>").replace(base_url, "<BASE_URL>")

def capture_request(base_url: str, app_path: Path, name: str, path: str, *, method: str = "GET", headers=None, data: bytes | None = None, timeout: float = 20.0, max_body_bytes: int | None = None) -> dict:
    request = urllib.request.Request(base_url + path, data=data, headers=headers or {}, method=method)
    started = time.monotonic()
    try:
        response = OPENER.open(request, timeout=timeout)
    except urllib.error.HTTPError as error:
        response = error
    body = response.read() if max_body_bytes is None else response.read(max_body_bytes)
    elapsed_ms = round((time.monotonic() - started) * 1000, 1)
    stable_headers = {}
    for key, value in response.headers.items():
        lower = key.lower()
        if lower in STABLE_HEADERS:
            stable_headers[lower] = normalize_location(value, base_url)
    entry = {
        "name": name,
        "request": {"method": method, "path": path, "headers": headers or {}},
        "response": {
            "status": response.status,
            "headers": dict(sorted(stable_headers.items())),
            "body_length": len(body),
            "body_sha256": hashlib.sha256(body).hexdigest(),
            "body_truncated": max_body_bytes is not None,
        },
        "elapsed_ms": elapsed_ms,
    }
    content_type = response.headers.get("Content-Type", "")
    if "json" in content_type and body:
        decoded = json.loads(body.decode("utf-8"))
        entry["response"]["json"] = normalize_json(decoded, base_url, app_path)
    elif len(body) <= 4096:
        entry["response"]["text"] = body.decode("utf-8", errors="replace").replace(base_url, "<BASE_URL>")
    return entry

class OracleProcess:
    def __init__(self, source: Path, fresh: bool = True):
        self.source = source.resolve()
        self.payload = LAB_ROOT / "payload"
        self.app_path = LAB_ROOT / "appdata"
        self.log_lines: list[str] = []
        self.ready = threading.Event()
        self.process: subprocess.Popen[str] | None = None
        self.reader: threading.Thread | None = None
        self.base_url = f"http://127.0.0.1:{LAB_PORT}"
        self.fresh = fresh
        self.shutdown_clean = False

    def verify_source(self) -> None:
        server = self.source / "server.js"
        runtime = self.source / "stremio-runtime.exe"
        if not server.is_file() or not runtime.is_file():
            raise RuntimeError(f"stream-server payload incomplete at {self.source}")
        if sha256(server) != SERVER_SHA256:
            raise RuntimeError("server.js hash does not match pinned 4.20.17 oracle")
        if sha256(runtime) != RUNTIME_SHA256:
            raise RuntimeError("stremio-runtime.exe hash does not match pinned oracle")

    def prepare(self) -> None:
        self.verify_source()
        ensure_port_free(LAB_PORT)
        LAB_ROOT.mkdir(parents=True, exist_ok=True)
        remove_tree_with_retries(self.payload)
        shutil.copytree(self.source, self.payload)
        server = self.payload / "server.js"
        data = server.read_bytes()
        if data.count(b"11470") != 6 or b"11480" in data:
            raise RuntimeError("port patch precondition failed")
        server.write_bytes(data.replace(b"11470", b"11480"))
        if self.fresh and self.app_path.exists():
            shutil.rmtree(self.app_path)
        self.app_path.mkdir(parents=True, exist_ok=True)

    def start(self) -> None:
        self.prepare()
        env = os.environ.copy()
        env.pop("NODE_OPTIONS", None)
        env["NO_HTTPS_SERVER"] = "1"
        env["APP_PATH"] = str(self.app_path)
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.process = subprocess.Popen(
            [str(self.payload / "stremio-runtime.exe"), "server.js"],
            cwd=self.payload,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=flags,
        )
        self.reader = threading.Thread(target=self._drain_output, daemon=True)
        self.reader.start()
        if not self.ready.wait(30.0):
            raise RuntimeError("oracle did not emit the EngineFS readiness handshake")
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            try:
                capture_request(self.base_url, self.app_path, "ready", "/settings", timeout=2.0)
                return
            except Exception:
                time.sleep(0.2)
        raise RuntimeError("oracle handshake appeared but /settings never became reachable")

    def _drain_output(self) -> None:
        assert self.process and self.process.stdout
        for line in self.process.stdout:
            line = line.rstrip("\r\n")
            self.log_lines.append(line)
            if line == f"EngineFS server started at {self.base_url}":
                self.ready.set()

    def stop(self) -> None:
        if not self.process:
            return
        terminate_process_tree(self.process)
        if self.reader:
            self.reader.join(timeout=2.0)
        if not wait_port_closed(LAB_PORT):
            raise RuntimeError("oracle shutdown leaked the lab listener")
        self.shutdown_clean = True

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        return False


def write_capture(path: Path, suite: str, records: list[dict], oracle: OracleProcess) -> None:
    document = {
        "schema": 1,
        "oracle": {
            "server_version": "4.20.17",
            "server_sha256": SERVER_SHA256,
            "runtime_sha256": RUNTIME_SHA256,
            "lab_port": LAB_PORT,
        },
        "suite": suite,
        "records": records,
        "startup_handshake_seen": oracle.ready.is_set(),
        "shutdown_clean": oracle.shutdown_clean,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")

def control_suite(oracle: OracleProcess) -> list[dict]:
    base, app = oracle.base_url, oracle.app_path
    records = [
        capture_request(base, app, "settings-default", "/settings"),
        capture_request(base, app, "heartbeat", "/heartbeat"),
        capture_request(base, app, "stats-empty", "/stats.json"),
        capture_request(base, app, "favicon", "/favicon.ico"),
        capture_request(base, app, "root-redirect", "/"),
        capture_request(base, app, "local-addon-manifest", "/local-addon/manifest.json"),
    ]
    payload = json.dumps({
        "btMaxConnections": 57,
        "btDownloadSpeedSoftLimit": 2621440,
        "btDownloadSpeedHardLimit": 3670016,
    }).encode("utf-8")
    records.append(capture_request(
        base, app, "settings-update", "/settings", method="POST",
        headers={"Content-Type": "application/json"}, data=payload,
    ))
    records.append(capture_request(base, app, "settings-effective", "/settings"))
    return records


def create_fixture_tree(root: Path, ffmpeg: Path) -> None:
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    blob = bytes(range(256)) * 4096
    (root / "blob.bin").write_bytes(blob)
    (root / "subtitle.srt").write_text(
        "1\n00:00:00,000 --> 00:00:00,750\nAqueduct oracle\n\n",
        encoding="utf-8",
    )
    sample = root / "sample.mp4"
    subprocess.run([
        str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "color=c=black:s=160x90:d=1:r=24",
        "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
        "-shortest", "-c:v", "libx264", "-preset", "ultrafast",
        "-c:a", "aac", "-movflags", "+faststart", str(sample),
    ], check=True, timeout=30.0)
    with zipfile.ZipFile(root / "sample.zip", "w", zipfile.ZIP_STORED) as archive:
        archive.write(sample, arcname="movie.mp4")
    with tarfile.open(root / "sample.tar", "w") as archive:
        archive.add(sample, arcname="movie.mp4")
    with tarfile.open(root / "sample.tgz", "w:gz") as archive:
        archive.add(sample, arcname="movie.mp4")


def quoted_url(url: str) -> str:
    return urllib.parse.quote(url, safe="")


def offline_suite(oracle: OracleProcess) -> list[dict]:
    fixture_root = LAB_ROOT / "fixtures"
    create_fixture_tree(fixture_root, oracle.payload / "ffmpeg.exe")
    ensure_port_free(FIXTURE_PORT)
    fixture = create_server(fixture_root, FIXTURE_PORT)
    thread = threading.Thread(target=fixture.serve_forever, daemon=True)
    thread.start()
    host, port = fixture.server_address
    origin = f"http://{host}:{port}"
    base, app = oracle.base_url, oracle.app_path
    records: list[dict] = []
    try:
        media_url = origin + "/sample.mp4"
        subtitle_url = origin + "/subtitle.srt"
        blob_url = origin + "/blob.bin"
        hls_path = "/url/" + quoted_url(media_url) + "/hls.m3u8"
        records.append(capture_request(base, app, "hls-url-master", hls_path, timeout=30.0))
        records.append(capture_request(
            base, app, "probe-url", "/probe?url=" + quoted_url(media_url), timeout=30.0,
        ))
        records.append(capture_request(
            base, app, "subtitles-srt", "/subtitles.srt?from=" + quoted_url(subtitle_url), timeout=20.0,
        ))
        proxy_opts = urllib.parse.urlencode({"d": origin})
        records.append(capture_request(
            base, app, "proxy-range", f"/proxy/{proxy_opts}/blob.bin",
            headers={"Range": "bytes=17-80"},
        ))
        archive_opts = quoted_url(json.dumps({"fileMustInclude": ["movie.mp4"]}))
        for archive_type, filename in (("zip", "sample.zip"), ("tar", "sample.tar"), ("tgz", "sample.tgz")):
            key = "wave0-" + archive_type
            body = json.dumps([origin + "/" + filename]).encode("utf-8")
            records.append(capture_request(
                base, app, f"{archive_type}-create", f"/{archive_type}/create/{key}",
                method="POST", headers={"Content-Type": "application/json"}, data=body,
            ))
            records.append(capture_request(
                base, app, f"{archive_type}-head", f"/{archive_type}/stream?key={key}&o={archive_opts}",
                method="HEAD", timeout=30.0,
            ))
            records.append(capture_request(
                base, app, f"{archive_type}-range", f"/{archive_type}/stream?key={key}&o={archive_opts}",
                headers={"Range": "bytes=0-127"}, timeout=30.0,
            ))
    finally:
        fixture.shutdown()
        fixture.server_close()
        thread.join(timeout=2.0)
    return records


def sample_stats(base: str, app: Path, hash_value: str, idx: int, name: str) -> dict:
    return capture_request(
        base, app, name, f"/{hash_value}/{idx}/stats.json", timeout=10.0,
    )


class RangePull(threading.Thread):
    def __init__(self, oracle: OracleProcess, name: str, hash_value: str, idx: int, range_header: str):
        super().__init__(daemon=True)
        self.oracle = oracle
        self.name = name
        self.hash_value = hash_value
        self.idx = idx
        self.range_header = range_header
        self.result: dict | None = None
        self.error: str | None = None

    def run(self) -> None:
        try:
            self.result = capture_request(
                self.oracle.base_url,
                self.oracle.app_path,
                self.name,
                f"/{self.hash_value}/{self.idx}",
                headers={"Range": self.range_header},
                timeout=120.0,
            )
        except Exception as exc:  # noqa: BLE001 - fixture records exact failure
            self.error = repr(exc)


TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce",
]

def live_suite(oracle: OracleProcess) -> list[dict]:
    base, app = oracle.base_url, oracle.app_path
    sources = ["dht:" + SINTEL_HASH] + ["tracker:" + url for url in TRACKERS]
    body = json.dumps({"sources": sources}).encode("utf-8")
    records: list[dict] = []
    records.append(capture_request(
        base, app, "engine-create", f"/{SINTEL_HASH}/create",
        method="POST", headers={"Content-Type": "application/json"}, data=body, timeout=30.0,
    ))
    race = RangePull(oracle, "metadata-race-range", SINTEL_HASH, SINTEL_INDEX, "bytes=0-65535")
    race.start()
    for attempt in range(5):
        try:
            records.append(sample_stats(base, app, SINTEL_HASH, SINTEL_INDEX, f"stats-early-{attempt}"))
            break
        except Exception as exc:  # noqa: BLE001
            records.append({"name": f"stats-early-{attempt}", "error": repr(exc)})
            time.sleep(0.5)
    race.join(timeout=120.0)
    if race.is_alive():
        raise RuntimeError("metadata-race range did not complete within 120 seconds")
    if race.error:
        raise RuntimeError(f"metadata-race range failed: {race.error}")
    assert race.result is not None
    records.append(race.result)
    records.append(capture_request(
        base, app, "media-head", f"/{SINTEL_HASH}/{SINTEL_INDEX}", method="HEAD", timeout=20.0,
    ))
    records.append(capture_request(
        base, app, "range-open-ended", f"/{SINTEL_HASH}/{SINTEL_INDEX}",
        headers={"Range": "bytes=0-"}, timeout=120.0, max_body_bytes=65536,
    ))
    records.append(capture_request(
        base, app, "range-bounded", f"/{SINTEL_HASH}/{SINTEL_INDEX}",
        headers={"Range": "bytes=1048576-1114111"}, timeout=120.0,
    ))
    records.append(capture_request(
        base, app, "range-seek", f"/{SINTEL_HASH}/{SINTEL_INDEX}",
        headers={"Range": "bytes=10485760-10551295"}, timeout=120.0,
    ))
    records.append(capture_request(
        base, app, "range-invalid", f"/{SINTEL_HASH}/{SINTEL_INDEX}",
        headers={"Range": "bytes=999999999999-1000000000000"}, timeout=20.0, max_body_bytes=65536,
    ))
    records.append(sample_stats(base, app, SINTEL_HASH, SINTEL_INDEX, "stats-after-ranges"))
    records.append(capture_request(base, app, "engine-remove", f"/{SINTEL_HASH}/remove", timeout=20.0))
    records.append(capture_request(base, app, "stats-after-remove", f"/{SINTEL_HASH}/stats.json", timeout=10.0))
    return records

SUITES = {
    "control": control_suite,
    "offline": offline_suite,
    "live": live_suite,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", choices=["control", "offline", "live", "all"])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument(
        "--output-dir", type=Path,
        default=SCRIPT.parent / "golden",
    )
    args = parser.parse_args()
    selected = list(SUITES) if args.suite == "all" else [args.suite]
    for suite_name in selected:
        oracle = OracleProcess(args.source, fresh=True)
        oracle.start()
        try:
            records = SUITES[suite_name](oracle)
        finally:
            oracle.stop()
        output = args.output_dir / f"{suite_name}.json"
        write_capture(output, suite_name, records, oracle)
        print(f"GREEN {suite_name}: {len(records)} records -> {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

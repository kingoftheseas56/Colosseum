#!/usr/bin/env python3
"""Minimal P02 adapter for the immutable Stremio reference runtime."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import urllib.request
from typing import Any

ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
HTTP_PORTS = tuple(range(11470, 11475))


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


@dataclass(frozen=True)
class Bundle:
    runtime: Path
    server: Path
    ffmpeg: Path
    ffprobe: Path

    def validate(self) -> None:
        for label, path in (("runtime", self.runtime), ("server", self.server), ("ffmpeg", self.ffmpeg), ("ffprobe", self.ffprobe)):
            if not path.is_file():
                raise FileNotFoundError(f"{label} missing: {path}")
        actual = sha256_file(self.server)
        if actual != ORACLE_SHA256:
            raise RuntimeError(f"oracle hash mismatch: {actual} != {ORACLE_SHA256}")

    def fingerprints(self) -> dict[str, str]:
        self.validate()
        return {k: sha256_file(v) for k, v in {
            "runtime": self.runtime, "server": self.server,
            "ffmpeg": self.ffmpeg, "ffprobe": self.ffprobe,
        }.items()}


def runtime_version(runtime: Path) -> str:
    p = subprocess.run([str(runtime), "--version"], text=True, capture_output=True, timeout=10)
    if p.returncode != 0:
        raise RuntimeError(f"runtime --version failed: {p.returncode}: {p.stderr}")
    return p.stdout.strip()


def request_json(port: int, path: str, timeout: float = 2.0) -> tuple[int, dict[str, Any]]:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout) as r:
        return r.status, json.loads(r.read().decode("utf-8"))


def wait_ready(proc: subprocess.Popen[str], timeout: float = 12.0) -> tuple[int, dict[str, Any], dict[str, Any]]:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"reference exited before readiness with {proc.returncode}")
        for port in HTTP_PORTS:
            try:
                hs, heartbeat = request_json(port, "/heartbeat", 0.3)
                ss, settings = request_json(port, "/settings", 0.6)
                if hs == 200 and heartbeat.get("success") is True and ss == 200:
                    return port, heartbeat, settings
            except Exception as exc:
                last_error = exc
        time.sleep(0.08)
    raise TimeoutError(f"reference did not become ready; last={last_error!r}")


def launch(bundle: Bundle, run_root: Path, *, media: bool = True) -> tuple[subprocess.Popen[str], Any, Any]:
    app = run_root / "app"
    settings = run_root / "settings"
    app.mkdir(parents=True, exist_ok=True)
    settings.mkdir(parents=True, exist_ok=True)
    stdout = (run_root / "stdout.txt").open("w", encoding="utf-8", newline="\n")
    stderr = (run_root / "stderr.txt").open("w", encoding="utf-8", newline="\n")
    env = os.environ.copy()
    env["APP_PATH"] = str(app)
    env["SETTINGS_PATH"] = str(settings)
    env["FFMPEG_BIN"] = str(bundle.ffmpeg if media else run_root / "missing-ffmpeg")
    env["FFPROBE_BIN"] = str(bundle.ffprobe if media else run_root / "missing-ffprobe")
    proc = subprocess.Popen([str(bundle.runtime), str(bundle.server)], cwd=str(run_root), env=env, stdout=stdout, stderr=stderr, text=True)
    return proc, stdout, stderr


def stop(proc: subprocess.Popen[str], stdout: Any, stderr: Any) -> int:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)
    stdout.close()
    stderr.close()
    return int(proc.returncode or 0)


def smoke(bundle: Bundle, out: Path) -> dict[str, Any]:
    bundle.validate()
    out.mkdir(parents=True, exist_ok=True)
    proc, so, se = launch(bundle, out)
    try:
        port, heartbeat, settings = wait_ready(proc)
        result = {
            "case": "P02-01", "passed": True, "port": port,
            "heartbeat": heartbeat, "settings": settings,
            "runtime_version": runtime_version(bundle.runtime),
            "hashes": bundle.fingerprints(),
        }
    finally:
        exit_code = stop(proc, so, se)
    result["teardown_exit"] = exit_code
    if result["settings"].get("values", {}).get("serverVersion") != "4.21.0":
        result["passed"] = False
        result["reason"] = "unexpected compatibility serverVersion"
    return result


def collision(bundle: Bundle, out: Path) -> dict[str, Any]:
    bundle.validate()
    out.mkdir(parents=True, exist_ok=True)
    held: list[socket.socket] = []
    try:
        for port in HTTP_PORTS:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("127.0.0.1", port))
            s.listen(1)
            held.append(s)
        proc, so, se = launch(bundle, out)
        time.sleep(2.2)
        alive = proc.poll() is None
        exit_before_stop = proc.poll()
        stop(proc, so, se)
    finally:
        for s in held:
            s.close()
    stderr_text = (out / "stderr.txt").read_text(encoding="utf-8", errors="replace")
    seen = [port for port in HTTP_PORTS if f"port: {port}" in stderr_text and "EADDRINUSE" in stderr_text]
    return {
        "case": "P02-02",
        "passed": alive and seen == list(HTTP_PORTS),
        "process_alive_after_exhaustion": alive,
        "exit_before_stop": exit_before_stop,
        "collision_ports_observed": seen,
    }


def missing_media(bundle: Bundle, out: Path) -> dict[str, Any]:
    bundle.validate()
    out.mkdir(parents=True, exist_ok=True)
    isolated = out / "isolated-runtime"
    isolated.mkdir(exist_ok=True)
    runtime_copy = isolated / bundle.runtime.name
    server_copy = isolated / "server.js"
    shutil.copy2(bundle.runtime, runtime_copy)
    shutil.copy2(bundle.server, server_copy)
    copy_bundle = Bundle(runtime_copy, server_copy, out / "missing-ffmpeg", out / "missing-ffprobe")
    if sha256_file(server_copy) != ORACLE_SHA256:
        raise RuntimeError("copied oracle hash mismatch")
    proc, so, se = launch(copy_bundle, out, media=False)
    try:
        port, heartbeat, settings = wait_ready(proc)
    finally:
        stop(proc, so, se)
    stdout_text = (out / "stdout.txt").read_text(encoding="utf-8", errors="replace")
    stderr_text = (out / "stderr.txt").read_text(encoding="utf-8", errors="replace")
    no_ffmpeg = "ffmpeg: null" in stdout_text
    no_ffprobe = "ffprobe: null" in stdout_text or "ffprobe: undefined" in stdout_text
    hw_probe_error = "ERR_INVALID_ARG_TYPE" in stderr_text and '"file" argument must be of type string' in stderr_text
    return {
        "case": "P02-03-media",
        "passed": no_ffmpeg and no_ffprobe and hw_probe_error and heartbeat.get("success") is True,
        "port": port,
        "heartbeat": heartbeat,
        "server_version": settings.get("values", {}).get("serverVersion"),
        "ffmpeg_missing_attributed": no_ffmpeg,
        "ffprobe_missing_attributed": no_ffprobe,
        "hardware_probe_spawn_error_attributed": hw_probe_error,
        "observed_ffprobe_marker": "undefined" if "ffprobe: undefined" in stdout_text else "null",
        "runtime_sha256": sha256_file(runtime_copy),
        "server_sha256": sha256_file(server_copy),
    }


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--runtime", required=True)
    p.add_argument("--server", required=True)
    p.add_argument("--ffmpeg", required=True)
    p.add_argument("--ffprobe", required=True)
    p.add_argument("--case", choices=["identity", "P02-01", "P02-02", "P02-03-media"], required=True)
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args()
    bundle = Bundle(Path(args.runtime), Path(args.server), Path(args.ffmpeg), Path(args.ffprobe))
    args.out.mkdir(parents=True, exist_ok=True)
    if args.case == "identity":
        bundle.validate()
        result = {"case": "identity", "passed": True, "runtime_version": runtime_version(bundle.runtime), "hashes": bundle.fingerprints()}
    elif args.case == "P02-01":
        result = smoke(bundle, args.out)
    elif args.case == "P02-02":
        result = collision(bundle, args.out)
    else:
        result = missing_media(bundle, args.out)
    (args.out / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())

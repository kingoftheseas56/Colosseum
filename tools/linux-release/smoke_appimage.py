#!/usr/bin/env python3
"""Actual AppImage startup/normal-exit proof, without FUSE or an installed Qt SDK."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import time

from assemble import sha
from package_appimage import image_name, validate_image, validate_stage, verify_abi

READY_MARKER = "COLOSSEUM_STARTUP_MILESTONE milestone=shell-interactive"


def make_environment(profile: Path, address: Path, inherited: dict) -> dict:
    environment = dict(inherited)
    for key in list(environment):
        if key.startswith(("QT_", "QML", "QTWEBENGINE", "COLOSSEUM_", "GDK_PIXBUF_")) or key in (
                "LD_LIBRARY_PATH", "LD_PRELOAD", "QTDIR", "QT_HOME", "XDG_CONFIG_DIRS",
                "XDG_DATA_DIRS", "SSL_CERT_FILE", "SSL_CERT_DIR"):
            environment.pop(key, None)
    environment.update({"HOME": str(profile), "XDG_CONFIG_HOME": str(profile / "config"),
                        "XDG_DATA_HOME": str(profile / "data"), "XDG_CACHE_HOME": str(profile / "cache"),
                        "XDG_RUNTIME_DIR": str(profile / "runtime"),
                        "COLOSSEUM_LANISTA_PIPE": str(address), "COLOSSEUM_LANISTA_DRIVE": "1",
                        "COLOSSEUM_STARTUP_PROBE": "1", "QT_QPA_PLATFORM": "offscreen",
                        "QT_FORCE_STDERR_LOGGING": "1", "QT_QUICK_BACKEND": "software",
                        "LIBGL_ALWAYS_SOFTWARE": "1"})
    return environment


def command(address: Path, name: str, payload: dict | None = None) -> dict:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(str(address))
        connection.sendall(json.dumps({"seq": 1, "cmd": name, "payload": payload or {}}).encode() + b"\n")
        data = bytearray()
        while b"\n" not in data:
            block = connection.recv(65536)
            if not block:
                raise RuntimeError("Lanista disconnected without a reply")
            data.extend(block)
            if len(data) > 2 * 1024 * 1024:
                raise RuntimeError("Lanista reply exceeded the bounded smoke response limit")
    reply = json.loads(data.split(b"\n", 1)[0])
    if not isinstance(reply, dict) or reply.get("type") != "reply" or reply.get("ok") is False or reply.get("error"):
        raise RuntimeError("Lanista rejected " + name + ": " + str(reply)[:1000])
    return reply


def ready(text: str, state: dict, splash: dict) -> bool:
    marked = re.search(re.escape(READY_MARKER) + r" atMs=[0-9]+(?:\s|$)", text) is not None
    windows = state.get("windows", [])
    visible = any(isinstance(w, dict) and w.get("visible") is True
                  and isinstance(w.get("width"), (int, float)) and w["width"] > 0
                  and isinstance(w.get("height"), (int, float)) and w["height"] > 0
                  for w in windows)
    return marked and visible and splash.get("props", {}).get("visible") is False


def validate_exit(code: int, became_ready: bool, quit_requested: bool) -> None:
    if code != 0 or not became_ready or not quit_requested:
        raise RuntimeError(f"Normal shutdown not proven: exit={code}, ready={became_ready}, quit_requested={quit_requested}")


def observe_and_quit(process, log_path: Path, address: Path, profile: Path,
                     startup_timeout: float, shutdown_timeout: float, ipc=command) -> dict:
    deadline = time.monotonic() + startup_timeout
    last_error = "No IPC response yet"
    state = {}
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Application exited before startup-ready: {process.returncode}")
        try:
            state = ipc(address, "get-state")
            splash = ipc(address, "qml-get", {"object": "bootSplash", "props": ["visible"]})
            if ready(log_path.read_text(errors="replace"), state, splash):
                break
        except (OSError, RuntimeError, ValueError) as error:
            last_error = str(error)
        time.sleep(0.2)
    else:
        raise RuntimeError(f"Startup deadline expired without shell-interactive proof: {last_error}")
    app_data = state.get("appDataRoot", "")
    if not app_data or not Path(app_data).resolve().is_relative_to((profile / "data").resolve()):
        raise RuntimeError("Application escaped its disposable data profile: " + app_data)
    ipc(address, "ui-keypress", {"key": "Ctrl+Q"})
    code = process.wait(timeout=shutdown_timeout)
    validate_exit(code, True, True)
    markers = [line for line in log_path.read_text(errors="replace").splitlines() if READY_MARKER in line]
    return {"exit_code": code, "startup_ready_log": markers[-1], "quit": "Lanista Ctrl+Q",
            "windows": state["windows"], "profile_isolated": True}


def cleanup_process(process, receipt: dict) -> None:
    """Reap only the launched process group before deleting its profile."""
    if process is None or process.poll() is not None:
        return
    receipt["forced_cleanup"] = True
    receipt["status"] = "FAIL"
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except ProcessLookupError:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--startup-timeout", type=float, default=120)
    parser.add_argument("--shutdown-timeout", type=float, default=20)
    args = parser.parse_args()
    if os.name != "posix" or os.getuid() == 0:
        parser.error("Run on Linux as a normal user, never root")
    if args.startup_timeout <= 0 or args.shutdown_timeout <= 0:
        parser.error("Timeouts must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {"status": "FAIL", "phase": "identity", "source_commit": args.source_commit,
               "scope": "Offscreen packaged startup and normal exit only; not GPU, playback or desktop qualification"}
    image = args.image.resolve()
    process = None
    try:
        expected = json.loads(args.manifest.read_text())
        validate_image(image)
        digest = sha(image)
        if digest != expected.get("sha256") or expected.get("source_commit") != args.source_commit:
            raise RuntimeError("AppImage SHA256 or source commit differs from build evidence")
        if expected.get("file") != image.name or expected.get("bytes") != image.stat().st_size:
            raise RuntimeError("AppImage name or size differs from build evidence")
        receipt.update({"image": image.name, "sha256": digest, "bytes": image.stat().st_size,
                        "os_release": Path("/etc/os-release").read_text()})
        # Inspect the actual image contents, not only a neighbouring build manifest.
        with tempfile.TemporaryDirectory(prefix="colosseum-ai-", dir="/tmp") as directory:
            profile = Path(directory)
            (profile / "runtime").mkdir(mode=0o700)
            address = profile / "ipc.sock"
            environment = make_environment(profile, address, os.environ)
            receipt["phase"] = "extract-and-verify"
            with (output / "extract.log").open("w") as log:
                subprocess.run([str(image), "--appimage-extract"], cwd=profile, env=environment,
                               stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)
            extracted = profile / "squashfs-root"
            identity = validate_stage(extracted, args.source_commit)
            report = json.loads((extracted / "package-check.json").read_text())
            verify_abi(report)
            if report.get("source_commit") != args.source_commit or image_name(identity) != image.name:
                raise RuntimeError("Embedded image identity disagrees with build evidence")
            if sha(extracted / "usr/bin/colosseum") != expected.get("binary_sha256"):
                raise RuntimeError("Embedded application binary hash mismatch")
            receipt["version"] = identity["version"]
            receipt["required_symbol_versions_max"] = report["required_symbol_versions_max"]
            shutil.rmtree(extracted)
            receipt["phase"] = "startup-and-shutdown"
            with (output / "startup.log").open("w") as log:
                process = subprocess.Popen([str(image), "--appimage-extract-and-run"], cwd=profile,
                                           env=environment, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    receipt.update(observe_and_quit(process, output / "startup.log", address, profile,
                                                   args.startup_timeout, args.shutdown_timeout))
                finally:
                    cleanup_process(process, receipt)
            receipt.update({"status": "PASS", "phase": "finished"})
    except Exception as error:
        receipt["error"] = str(error)
    finally:
        try:
            cleanup_process(process, receipt)
        except Exception as cleanup_error:
            receipt["status"] = "FAIL"
            receipt["cleanup_error"] = str(cleanup_error)
        (output / "smoke.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt))
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

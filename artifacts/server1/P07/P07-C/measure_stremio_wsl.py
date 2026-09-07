"""Run the real authenticated Stremio route baseline in WSL.

This packet-local evidence runner uses the already qualified portable Node
v22.16.0 lane from P02.  It never edits the oracle, injects a fake player, or
turns an unavailable Windows bind into a pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import signal
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


NODE_VERSION = "v22.16.0"
NODE_ARCHIVE_URL = "https://nodejs.org/dist/v22.16.0/node-v22.16.0-linux-x64.tar.xz"
NODE_ARCHIVE_SHA256 = "f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e"
ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
PORTS = tuple(range(11470, 11475))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def request(port: int, route: str, timeout: float) -> dict[str, Any]:
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(
            urllib.request.Request(
                f"http://127.0.0.1:{port}{route}",
                headers={"Host": f"127.0.0.1:{port}"},
            ),
            timeout=timeout,
        ) as response:
            body_bytes = response.read()
            body = body_bytes.decode("utf-8", errors="replace")
            try:
                parsed: Any = json.loads(body)
            except json.JSONDecodeError:
                parsed = None
            return {
                "route": route,
                "status": response.status,
                "headers": dict(response.headers.items()),
                "body": body,
                "json": parsed,
                "elapsed_seconds": time.perf_counter() - started,
            }
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")
        return {
            "route": route,
            "status": error.code,
            "headers": dict(error.headers.items()),
            "body": body,
            "json": None,
            "elapsed_seconds": time.perf_counter() - started,
            "error": f"HTTPError: {error}",
        }
    except Exception as error:  # Preserve every failed observation in raw evidence.
        return {
            "route": route,
            "status": None,
            "headers": {},
            "body": "",
            "json": None,
            "elapsed_seconds": time.perf_counter() - started,
            "error": f"{type(error).__name__}: {error}",
        }


def ensure_node(cache_root: Path) -> tuple[Path, dict[str, Any]]:
    node_path = cache_root / "node-v22.16.0-linux-x64" / "bin" / "node"
    cache_root.mkdir(parents=True, exist_ok=True)
    archive = cache_root / "node-v22.16.0-linux-x64.tar.xz"
    reused = node_path.is_file()
    if not reused:
        with urllib.request.urlopen(NODE_ARCHIVE_URL, timeout=120) as response, archive.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        archive_sha = sha256_file(archive)
        if archive_sha != NODE_ARCHIVE_SHA256:
            raise RuntimeError(f"portable Node archive hash mismatch: {archive_sha}")
        with tarfile.open(archive, mode="r:xz") as bundle:
            bundle.extractall(cache_root)
    if not node_path.is_file():
        raise RuntimeError(f"portable Node executable missing after qualification: {node_path}")
    version = subprocess.run([str(node_path), "--version"], check=False, capture_output=True, text=True, timeout=10)
    reported_version = (version.stdout or version.stderr).strip().splitlines()[0]
    if version.returncode != 0 or reported_version != NODE_VERSION:
        raise RuntimeError(f"portable Node version mismatch: {reported_version!r}")
    return node_path, {
        "kind": "portable-node",
        "version": reported_version,
        "path": str(node_path),
        "sha256": sha256_file(node_path),
        "archive_url": NODE_ARCHIVE_URL,
        "archive_sha256": NODE_ARCHIVE_SHA256,
        "archive_reused": reused,
        "official_checksum_match": True,
    }


def stop_process(process: subprocess.Popen[str]) -> dict[str, Any]:
    started = time.perf_counter()
    method = "already-exited"
    if process.poll() is None:
        method = "SIGTERM-process-group"
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            method = "SIGKILL-process-group"
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
    return {
        "exit": process.returncode,
        "alive_after": process.poll() is None,
        "method": method,
        "elapsed_seconds": time.perf_counter() - started,
    }


def run_trial(
    *,
    node: Path,
    oracle: Path,
    output_dir: Path,
    raw_prefix: str,
    phase: str,
    trial: int,
    timeout_seconds: float,
) -> dict[str, Any]:
    trial_name = f"{phase}-{trial:02d}.json"
    raw_path = output_dir / trial_name
    oracle_before = sha256_file(oracle)
    with tempfile.TemporaryDirectory(prefix=f"colosseum-server1-p07-c-{phase}-{trial:02d}-") as temp:
        root = Path(temp)
        app_path = root / "app"
        settings_path = root / "settings"
        app_path.mkdir()
        settings_path.mkdir()
        (settings_path / "server-settings.json").write_text("{}\n", encoding="utf-8")
        stdout_path = root / "stdout.txt"
        environment = os.environ.copy()
        environment.update(
            {
                "APP_PATH": str(app_path),
                "SETTINGS_PATH": str(settings_path),
                "NO_HTTPS_SERVER": "1",
                "NO_NETWORK_INTERFACES": "1",
                "CASTING_DISABLED": "1",
                "DISABLE_CACHING": "1",
                "HLS_V2_DISABLED": "1",
                "FFMPEG_BIN": "/usr/bin/ffmpeg",
                "FFPROBE_BIN": "/usr/bin/ffprobe",
            }
        )
        command = [str(node), str(oracle)]
        started_at = utc_now()
        start_clock = time.perf_counter()
        process: subprocess.Popen[str] | None = None
        heartbeat_ready: dict[str, Any] | None = None
        heartbeat_ready_elapsed: float | None = None
        heartbeat: dict[str, Any] | None = None
        settings: dict[str, Any] | None = None
        failure: str | None = None
        with stdout_path.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(
                command,
                cwd=str(oracle.parent),
                env=environment,
                stdout=stream,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
            )
            deadline = time.perf_counter() + timeout_seconds
            while time.perf_counter() < deadline and process.poll() is None:
                for port in PORTS:
                    candidate = request(port, "/heartbeat", timeout=0.25)
                    if candidate.get("status") == 200:
                        heartbeat_ready = candidate
                        heartbeat_ready_elapsed = time.perf_counter() - start_clock
                        break
                if heartbeat_ready is not None:
                    break
                time.sleep(0.02)
            if heartbeat_ready is None:
                failure = "reference did not serve /heartbeat before timeout"
            else:
                port = PORTS[0]
                heartbeat = request(port, "/heartbeat", timeout=0.5)
                settings = request(port, "/settings", timeout=0.5)
        teardown = stop_process(process)
        transcript = stdout_path.read_text(encoding="utf-8", errors="replace")
        oracle_after = sha256_file(oracle)
        sample: dict[str, Any] = {
            "phase": phase,
            "trial": trial,
            "started_at_utc": started_at,
            "finished_at_utc": utc_now(),
            "command": command,
            "environment": {
                key: environment[key]
                for key in (
                    "APP_PATH",
                    "SETTINGS_PATH",
                    "NO_HTTPS_SERVER",
                    "NO_NETWORK_INTERFACES",
                    "CASTING_DISABLED",
                    "DISABLE_CACHING",
                    "HLS_V2_DISABLED",
                    "FFMPEG_BIN",
                    "FFPROBE_BIN",
                )
            },
            "oracle_sha256": oracle_after,
            "oracle_before_sha256": oracle_before,
            "heartbeat_ready": heartbeat_ready,
            "heartbeat": heartbeat,
            "settings": settings,
            "startup_seconds": heartbeat_ready_elapsed,
            "heartbeat_request_seconds": heartbeat["elapsed_seconds"] if heartbeat else None,
            "settings_request_seconds": settings["elapsed_seconds"] if settings else None,
            "teardown_seconds": teardown["elapsed_seconds"],
            "teardown_alive_after": teardown["alive_after"],
            "process_exit": teardown["exit"],
            "teardown": teardown,
            "failure": failure,
            "transcript": transcript,
            "paths_removed": True,
        }
        if sample["startup_seconds"] is None:
            sample["startup_seconds"] = time.perf_counter() - start_clock
        sample["result"] = (
            "PASS"
            if heartbeat_ready
            and heartbeat
            and heartbeat.get("status") == 200
            and heartbeat.get("json") == {"success": True}
            and settings
            and settings.get("status") == 200
            and isinstance(settings.get("json"), dict)
            and settings["json"].get("values", {}).get("serverVersion") == "4.21.0"
            and oracle_before == ORACLE_SHA256
            and oracle_after == ORACLE_SHA256
            and teardown["alive_after"] is False
            else "FAIL"
        )
        raw_path.write_text(json.dumps(sample, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    sample["raw_output"] = f"{raw_prefix}/{trial_name}"
    return sample


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--result", required=True, type=Path)
    parser.add_argument("--raw-prefix", required=True)
    parser.add_argument("--warmup-count", type=int, default=2)
    parser.add_argument("--measured-count", type=int, default=10)
    parser.add_argument("--timeout-seconds", type=float, default=6.0)
    args = parser.parse_args()
    if args.measured_count < 10:
        raise SystemExit("measured count must be at least ten")
    if not args.oracle.is_file() or sha256_file(args.oracle) != ORACLE_SHA256:
        raise SystemExit("oracle identity mismatch")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    node, runtime = ensure_node(Path("/tmp/colosseum-server1-p07-c-node-v22.16.0"))
    warmups = [
        run_trial(
            node=node,
            oracle=args.oracle,
            output_dir=args.output_dir,
            raw_prefix=args.raw_prefix,
            phase="warmup",
            trial=index,
            timeout_seconds=args.timeout_seconds,
        )
        for index in range(1, args.warmup_count + 1)
    ]
    samples = [
        run_trial(
            node=node,
            oracle=args.oracle,
            output_dir=args.output_dir,
            raw_prefix=args.raw_prefix,
            phase="measured",
            trial=index,
            timeout_seconds=args.timeout_seconds,
        )
        for index in range(1, args.measured_count + 1)
    ]
    result = {
        "schema": "colosseum-server1-p07-c-measurement/v1",
        "profile": "cold_process_control_routes",
        "contract": {
            "warmup_count": args.warmup_count,
            "measured_count": args.measured_count,
            "timeout_seconds": args.timeout_seconds,
            "routes": ["/heartbeat", "/settings"],
        },
        "oracle": {
            "path": str(args.oracle),
            "bytes": args.oracle.stat().st_size,
            "sha256": sha256_file(args.oracle),
        },
        "runtime": runtime,
        "warmups": warmups,
        "samples": samples,
        "status": "PASS" if all(item["result"] == "PASS" for item in [*warmups, *samples]) else "FAIL",
    }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "warmups": len(warmups), "samples": len(samples), "result": str(args.result)}))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

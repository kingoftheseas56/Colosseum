from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
ORACLE = Path(
    r"C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js"
)
ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
QUALIFIED_NODE_ENV = "P05_QUALIFIED_NODE"
QUALIFIED_NODE_VERSION = "v22.16.0"
PORT_START = 11_470
PORT_END = 11_474
STARTUP_TIMEOUT_SECONDS = 20.0
PROBE_TIMEOUT_SECONDS = 0.75
TEARDOWN_TIMEOUT_SECONDS = 5.0
LOG_TAIL_BYTES = 6_000
HANDSHAKE = re.compile(r"EngineFS server started at http://127\.0\.0\.1:(\d+)")
PROBE_PATHS = ("/heartbeat", "/settings", "/network-info")
PROFILE_RUNS = OUT / "profile-runs.json"
PROFILE_STDOUT = OUT / "profile-runs.stdout.json"
PROFILES = json.loads(
    (ROOT / "tools/server_lab/scenarios/reference_profiles.json").read_text(
        encoding="utf-8"
    )
)["profiles"]


class ReferenceProfileError(RuntimeError):
    """Raised when the controlled profile run cannot be performed honestly."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_version(node: Path) -> str:
    completed = subprocess.run(
        [str(node), "--version"],
        check=False,
        capture_output=True,
        text=True,
        timeout=10,
    )
    output = (completed.stdout or completed.stderr).strip()
    if completed.returncode != 0 or not output:
        raise ReferenceProfileError(
            f"qualified Node version command failed ({completed.returncode}): {output}"
        )
    return output.splitlines()[0]


def _qualified_runtime() -> tuple[Path, str]:
    value = os.environ.get(QUALIFIED_NODE_ENV)
    if not value:
        raise ReferenceProfileError(
            f"{QUALIFIED_NODE_ENV} must name the accepted portable Node {QUALIFIED_NODE_VERSION} executable"
        )
    node = Path(value).expanduser().resolve()
    if not node.is_file():
        raise ReferenceProfileError(f"qualified Node executable is missing: {node}")
    version = _read_version(node)
    if version != QUALIFIED_NODE_VERSION:
        raise ReferenceProfileError(
            f"qualified Node version mismatch: expected {QUALIFIED_NODE_VERSION}, got {version}"
        )
    return node, version


def _executable_identity(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"status": "missing", "path": None}
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        return {"status": "missing", "path": str(resolved)}
    return {
        "status": "present",
        "path": str(resolved),
        "bytes": resolved.stat().st_size,
        "sha256": _sha256(resolved),
    }


def _companion_report(name: str) -> dict[str, Any]:
    key = f"{name.upper()}_BIN"
    override = os.environ.get(key)
    if override:
        override_path = Path(override).expanduser()
        if override_path.is_file():
            result = _executable_identity(override_path)
            result["provenance"] = f"{key} environment override"
            return result
        return {
            "status": "missing",
            "path": None,
            "provenance": f"ignored invalid {key} environment override",
            "invalid_override": override,
        }
    resolved = shutil.which(name)
    if not resolved:
        return {
            "status": "missing",
            "path": None,
            "provenance": "host PATH; no repair or substituted path",
        }
    result = _executable_identity(Path(resolved))
    result["provenance"] = "host PATH"
    return result


def _profile_is_source_only(profile: dict[str, Any]) -> bool:
    execution = profile.get("execution", {})
    if platform.system() == "Windows":
        return execution.get("windows") == "source-traced-only"
    return execution.get("non_windows") == "source-traced-only"


def _port_probe(port: int) -> dict[str, Any]:
    errors: list[dict[str, Any]] = []
    for family, address in (
        (socket.AF_INET6, ("::", port)),
        (socket.AF_INET, ("127.0.0.1", port)),
    ):
        probe = socket.socket(family, socket.SOCK_STREAM)
        try:
            if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            probe.bind(address)
            return {"status": "available", "family": family}
        except OSError as error:
            errors.append(
                {
                    "family": family,
                    "errno": error.errno,
                    "message": str(error),
                }
            )
        finally:
            probe.close()
    return {"status": "unavailable", "errors": errors}


def _port_preflight() -> dict[str, Any]:
    ports = {str(port): _port_probe(port) for port in range(PORT_START, PORT_END + 1)}
    available = [int(port) for port, result in ports.items() if result["status"] == "available"]
    return {
        "range": [PORT_START, PORT_END],
        "ports": ports,
        "available_candidates": available,
        "selection": "first-available-source-fallback-range" if available else "none-available",
    }


def _request(port: int, path: str) -> dict[str, Any]:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        headers={"Host": f"127.0.0.1:{port}"},
    )
    try:
        with urllib.request.urlopen(request, timeout=PROBE_TIMEOUT_SECONDS) as response:
            body = response.read().decode("utf-8", errors="replace")
            return {
                "status": response.status,
                "headers": dict(response.headers.items()),
                "body": body[:4096],
            }
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")
        return {
            "status": error.code,
            "headers": dict(error.headers.items()),
            "body": body[:4096],
        }
    except (urllib.error.URLError, TimeoutError, ConnectionError, socket.timeout) as error:
        return {"error": type(error).__name__, "message": str(error)}


def _read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def _wait_for_handshake(process: subprocess.Popen[Any], log_path: Path) -> dict[str, Any]:
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        transcript = _read_log(log_path)
        matches = [int(value) for value in HANDSHAKE.findall(transcript)]
        valid = [port for port in matches if PORT_START <= port <= PORT_END]
        if valid:
            return {"ready": True, "port": valid[-1], "transcript": transcript}
        if process.poll() is not None:
            break
        time.sleep(0.05)
    transcript = _read_log(log_path)
    matches = [int(value) for value in HANDSHAKE.findall(transcript)]
    valid = [port for port in matches if PORT_START <= port <= PORT_END]
    return {
        "ready": bool(valid),
        "port": valid[-1] if valid else None,
        "transcript": transcript,
    }


def _stop_process(process: subprocess.Popen[Any]) -> dict[str, Any]:
    if process.poll() is not None:
        return {
            "method": "already-exited",
            "exit": process.returncode,
            "alive_after": False,
        }

    if os.name == "nt":
        command = ["taskkill", "/PID", str(process.pid), "/T", "/F"]
        killed = subprocess.run(command, check=False, capture_output=True, text=True)
        try:
            process.wait(timeout=TEARDOWN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=TEARDOWN_TIMEOUT_SECONDS)
        return {
            "method": "taskkill-process-tree",
            "command": command,
            "taskkill_exit": killed.returncode,
            "taskkill_stdout": killed.stdout[-1000:],
            "taskkill_stderr": killed.stderr[-1000:],
            "exit": process.returncode,
            "alive_after": process.poll() is None,
        }

    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=TEARDOWN_TIMEOUT_SECONDS)
        method = "SIGTERM-process-group"
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=TEARDOWN_TIMEOUT_SECONDS)
        method = "SIGKILL-process-group"
    return {
        "method": method,
        "exit": process.returncode,
        "alive_after": process.poll() is None,
    }


def _child_environment(profile: dict[str, Any], app_path: Path, settings_path: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment.update(
        {
            "APP_PATH": str(app_path),
            "SETTINGS_PATH": str(settings_path),
            "NO_HTTPS_SERVER": "1",
            "NO_NETWORK_INTERFACES": "1",
        }
    )
    for key, value in profile.get("env", {}).items():
        if not value.startswith("<"):
            environment[key] = value

    for name in ("FFMPEG_BIN", "FFPROBE_BIN"):
        value = environment.get(name)
        if value and not Path(value).expanduser().is_file():
            environment.pop(name, None)
    return environment


def _environment_record(environment: dict[str, str]) -> dict[str, str]:
    keys = (
        "APP_PATH",
        "SETTINGS_PATH",
        "TV_ENV",
        "UNITY_ENV",
        "IOS_APP",
        "NO_HTTPS_SERVER",
        "NO_NETWORK_INTERFACES",
    )
    return {key: environment[key] for key in keys if key in environment}


def _observed_surfaces(port: int | None, observations: dict[str, Any], ready: bool) -> list[str]:
    if not ready or port is None:
        return []
    surfaces: list[str] = ["service.enginefs", "side-effect.http-listener"]
    if observations.get("/heartbeat", {}).get("status") == 200:
        surfaces.append("route.heartbeat")
    if observations.get("/settings", {}).get("status") == 200:
        surfaces.append("route.settings-get")
    if observations.get("/network-info", {}).get("status") == 200:
        surfaces.append("route.network-info")
    return surfaces


def _cleanup(run_root: Path) -> dict[str, Any]:
    try:
        shutil.rmtree(run_root)
    except FileNotFoundError:
        pass
    except OSError as error:
        return {"removed": False, "error": str(error)}
    return {"removed": not run_root.exists()}


def _run_profile(profile_id: str, profile: dict[str, Any], node: Path, version: str) -> tuple[dict[str, Any], str]:
    base = {
        "profile_id": profile_id,
        "platform": profile["platform"],
        "runtime": {"path": str(node), "version": version},
        "port_range": [PORT_START, PORT_END],
        "observed_surfaces": [],
        "observations": {},
    }
    if _profile_is_source_only(profile):
        return (
            {
                **base,
                "status": "NOT_RUN",
                "state": "SOURCE_TRACED_ONLY",
                "reason": profile["execution"].get(
                    "windows" if platform.system() == "Windows" else "non_windows",
                    "profile requires a native platform binding",
                ),
                "ready": False,
                "teardown": {"method": "not-started", "alive_after": False},
                "cleanup": {"removed": True, "method": "not-started"},
            },
            "",
        )

    run_root = Path(tempfile.mkdtemp(prefix=f"p02s-a-{profile_id}-", dir=OUT))
    app_path = run_root / "app"
    settings_path = run_root / "settings"
    log_path = run_root / "server.log"
    app_path.mkdir()
    settings_path.mkdir()
    settings_file = settings_path / "server-settings.json"
    settings_file.write_text("{}\n", encoding="utf-8")
    environment = _child_environment(profile, app_path, settings_path)
    preflight = _port_preflight()
    process: subprocess.Popen[Any] | None = None
    teardown: dict[str, Any] = {"method": "not-started", "alive_after": False}
    transcript = ""
    result: dict[str, Any]

    try:
        if not preflight["available_candidates"]:
            result = {
                **base,
                "status": "UNSUPPORTED",
                "state": "BLOCKED_ENV",
                "reason": "no collision-free source listener port is available",
                "ready": False,
                "command": [str(node), str(ORACLE)],
                "environment": _environment_record(environment),
                "paths": {
                    "run_root": str(run_root),
                    "app": str(app_path),
                    "settings": str(settings_path),
                    "settings_json": str(settings_file),
                },
                "settings_json_created": settings_file.is_file(),
                "port_preflight": preflight,
                "teardown": teardown,
            }
        else:
            log_stream = log_path.open("w", encoding="utf-8")
            kwargs: dict[str, Any] = {
                "cwd": str(ORACLE.parent),
                "env": environment,
                "stdout": log_stream,
                "stderr": subprocess.STDOUT,
            }
            if os.name == "nt":
                kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                kwargs["start_new_session"] = True
            try:
                process = subprocess.Popen([str(node), str(ORACLE)], **kwargs)
            finally:
                log_stream.close()

            handshake = _wait_for_handshake(process, log_path)
            transcript = handshake["transcript"]
            port = handshake["port"]
            observations: dict[str, Any] = {}
            if handshake["ready"] and port is not None:
                observations = {path: _request(port, path) for path in PROBE_PATHS}
            teardown = _stop_process(process)
            transcript = _read_log(log_path)
            observed = _observed_surfaces(port, observations, handshake["ready"])
            core_probe_pass = (
                observations.get("/heartbeat", {}).get("status") == 200
                and observations.get("/settings", {}).get("status") == 200
            )
            if handshake["ready"] and core_probe_pass and not teardown["alive_after"]:
                status = "PASS"
                state = "RUNTIME_OBSERVED"
                reason = "startup handshake and core probes passed"
            elif not handshake["ready"]:
                lowered = transcript.lower()
                blocked = any(
                    marker in lowered
                    for marker in (
                        "eaddrinuse",
                        "eacces",
                        "eaddrnotavail",
                        "address already in use",
                        "access is denied",
                    )
                )
                status = "UNSUPPORTED" if blocked else "ERROR"
                state = "BLOCKED_ENV" if blocked else "STARTUP_FAILED"
                reason = "source listener could not complete its bounded startup handshake"
            else:
                status = "ERROR"
                state = "PROBE_OR_TEARDOWN_FAILED"
                reason = "startup completed but required probes or teardown did not pass"
            result = {
                **base,
                "status": status,
                "state": state,
                "reason": reason,
                "ready": handshake["ready"],
                "port": port,
                "command": [str(node), str(ORACLE)],
                "environment": _environment_record(environment),
                "paths": {
                    "run_root": str(run_root),
                    "app": str(app_path),
                    "settings": str(settings_path),
                    "settings_json": str(settings_file),
                },
                "settings_json_created": settings_file.is_file(),
                "port_preflight": preflight,
                "observations": observations,
                "observed_surfaces": observed,
                "teardown": teardown,
                "exit_code": process.returncode,
            }
    except Exception as error:
        if process is not None and process.poll() is None:
            teardown = _stop_process(process)
        transcript = _read_log(log_path)
        result = {
            **base,
            "status": "ERROR",
            "state": "RUNNER_ERROR",
            "reason": f"{type(error).__name__}: {error}",
            "ready": False,
            "command": [str(node), str(ORACLE)],
            "environment": _environment_record(environment),
            "paths": {
                "run_root": str(run_root),
                "app": str(app_path),
                "settings": str(settings_path),
                "settings_json": str(settings_file),
            },
            "settings_json_created": settings_file.is_file(),
            "port_preflight": preflight,
            "teardown": teardown,
        }
    finally:
        if process is not None and process.stdout is not None:
            process.stdout.close()

    cleanup = _cleanup(run_root)
    result["cleanup"] = cleanup
    result["paths_removed_after_cleanup"] = cleanup["removed"]
    if result["status"] == "PASS" and not cleanup["removed"]:
        result["status"] = "ERROR"
        result["state"] = "CLEANUP_FAILED"
        result["reason"] = "profile run passed probes but its disposable root was not removed"
    result["stdout_tail"] = transcript[-LOG_TAIL_BYTES:]
    return result, transcript


def _accepted_wsl_evidence() -> dict[str, Any]:
    receipt = ROOT / "artifacts/server1/P02/CASE-RESULTS.json"
    if not receipt.is_file():
        return {
            "status": "NOT_AVAILABLE",
            "source": "artifacts/server1/P02/CASE-RESULTS.json",
        }
    payload = json.loads(receipt.read_text(encoding="utf-8"))
    case = next(item for item in payload["cases"] if item["id"] == "P02-01")
    return {
        "status": "PASS_WITH_PROVENANCE",
        "source": "artifacts/server1/P02/CASE-RESULTS.json",
        "receipt_sha256": _sha256(receipt),
        "case_id": case["id"],
        "lane": case["lane"],
        "runtime": case["command"][0],
        "runtime_version": "v22.16.0",
        "port": case["port"],
        "observed_surfaces": [
            "service.enginefs",
            "side-effect.http-listener",
            "route.heartbeat",
            "route.settings-get",
        ],
        "not_a_windows_profile_substitution": True,
    }


def _run_all() -> tuple[dict[str, Any], list[dict[str, Any]]]:
    if not ORACLE.is_file():
        raise ReferenceProfileError(f"oracle not found: {ORACLE}")
    oracle_before = _sha256(ORACLE)
    if oracle_before != ORACLE_SHA256:
        raise ReferenceProfileError(
            f"refusing profile run: oracle identity mismatch ({oracle_before})"
        )
    node, version = _qualified_runtime()
    results: list[dict[str, Any]] = []
    stdout_records: list[dict[str, Any]] = []
    for profile_id, profile in PROFILES.items():
        result, transcript = _run_profile(profile_id, profile, node, version)
        results.append(result)
        stdout_records.append(
            {
                "profile_id": profile_id,
                "status": result["status"],
                "stdout": transcript,
            }
        )
    oracle_after = _sha256(ORACLE)
    if oracle_after != oracle_before:
        raise ReferenceProfileError("oracle changed during profile qualification")
    payload = {
        "schema": "colosseum-server1-p02s-a-profile-runs/v2",
        "worker_id": "P02S-A",
        "oracle": {
            "path": str(ORACLE),
            "sha256": oracle_after,
            "unchanged": oracle_after == oracle_before,
        },
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "runtime": {
            "kind": "portable-node",
            "selection": QUALIFIED_NODE_ENV,
            "path": str(node),
            "version": version,
            "bytes": node.stat().st_size,
            "sha256": _sha256(node),
        },
        "companions": {
            "ffmpeg": _companion_report("ffmpeg"),
            "ffprobe": _companion_report("ffprobe"),
        },
        "port_range": [PORT_START, PORT_END],
        "accepted_external_evidence": _accepted_wsl_evidence(),
        "profiles": results,
    }
    return payload, stdout_records


def main() -> int:
    payload, stdout_records = _run_all()
    PROFILE_RUNS.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    PROFILE_STDOUT.write_text(
        json.dumps(
            {
                "schema": "colosseum-server1-p02s-a-profile-stdout/v1",
                "worker_id": "P02S-A",
                "runtime": payload["runtime"],
                "profiles": stdout_records,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

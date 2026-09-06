"""Isolated launcher and identity probe for the authenticated Stremio bundle.

This module deliberately treats the JavaScript bundle as opaque input.  It does
not patch, copy, or otherwise transform ``server.js`` before launching it.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Mapping, Sequence


ORACLE_BYTES = 6_676_503
ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
DEFAULT_PORT_START = 11_470
DEFAULT_PORT_END = 11_474


class ReferenceRuntimeError(RuntimeError):
    """Raised when the reference cannot be qualified without guessing."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _executable_identity(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.is_file()}
    if path.is_file():
        result["bytes"] = path.stat().st_size
        result["sha256"] = _sha256(path)
    return result


def _runtime_path(explicit: str | os.PathLike[str] | None) -> Path:
    candidates: list[str] = []
    if explicit:
        candidates.append(os.fspath(explicit))
    for variable in ("STREMIO_RUNTIME",):
        if os.environ.get(variable):
            candidates.append(os.environ[variable])
    candidates.extend(("stremio-runtime", "stremio-runtime.exe", "node"))
    for candidate in candidates:
        resolved = shutil.which(candidate)
        path = Path(candidate)
        if resolved:
            return Path(resolved).resolve()
        if path.is_file():
            return path.resolve()
    raise ReferenceRuntimeError(
        "reference runtime unavailable: provide STREMIO_RUNTIME or install node"
    )


def _version(path: Path) -> str:
    completed = subprocess.run(
        [str(path), "--version"],
        check=False,
        capture_output=True,
        text=True,
        timeout=10,
    )
    output = (completed.stdout or completed.stderr).strip()
    if completed.returncode != 0 or not output:
        raise ReferenceRuntimeError(
            f"runtime version command failed ({completed.returncode}): {output}"
        )
    return output.splitlines()[0]


def _metadata(source: bytes) -> dict[str, object]:
    def match(pattern: bytes, label: str) -> str:
        found = re.search(pattern, source)
        if not found:
            raise ReferenceRuntimeError(f"oracle metadata missing: {label}")
        return found.group(1).decode("utf-8")

    runtime_version = match(rb'stremioRuntimeVersion:\s*"([^"]+)"', "stremioRuntimeVersion")
    package = re.search(
        rb'name:\s*"(stremio-server)"\s*,\s*version:\s*"([^"]+)"\s*,\s*stremioRuntimeVersion:\s*"([^"]+)"',
        source,
    )
    if not package:
        raise ReferenceRuntimeError("oracle metadata missing: stremio-server package block")
    package_name, package_version, package_runtime = (
        value.decode("utf-8") for value in package.groups()
    )
    if package_runtime != runtime_version:
        raise ReferenceRuntimeError("oracle metadata contains inconsistent runtime versions")
    return {
        "name": package_name,
        "version": package_version,
        "stremioRuntimeVersion": runtime_version,
        "stremioRuntimeVersionIsNodeSemver": bool(
            re.fullmatch(r"v?\d+\.\d+\.\d+", runtime_version)
        ),
    }


def _candidate_paths(
    name: str,
    runtime: Path,
    environment: Mapping[str, str],
    search_roots: Sequence[Path],
) -> list[Path]:
    candidates: list[Path] = []
    override = environment.get(f"{name.upper()}_BIN", "")
    if override:
        candidates.append(Path(override))
    runtime_dir = runtime.parent
    roots = [runtime_dir, *search_roots]
    for root in roots:
        candidates.extend(
            (
                root / name,
                root / f"{name}.exe",
                root / "bin" / name,
                root / "bin" / f"{name}.exe",
            )
        )
    path_value = environment.get("PATH", "")
    if path_value:
        for entry in path_value.split(os.pathsep):
            if entry:
                root = Path(entry)
                candidates.extend((root / name, root / f"{name}.exe"))
    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = os.path.normcase(os.path.abspath(os.fspath(candidate)))
        if key not in seen:
            unique.append(candidate)
            seen.add(key)
    return unique


class StremioReference:
    """Launch the supplied bundle in a disposable lab run with loopback probes."""

    def __init__(
        self,
        server_js: Path,
        runtime: str | os.PathLike[str] | None = None,
        *,
        port_start: int = DEFAULT_PORT_START,
        port_end: int = DEFAULT_PORT_END,
    ) -> None:
        self.server_js = Path(server_js).resolve()
        if not self.server_js.is_file():
            raise ReferenceRuntimeError(f"oracle not found: {self.server_js}")
        self.runtime = _runtime_path(runtime)
        self.port_start = port_start
        self.port_end = port_end

    def fingerprint(self) -> dict[str, object]:
        source_bytes = self.server_js.read_bytes()
        runtime_kind = "stremio-runtime" if "stremio-runtime" in self.runtime.stem else "node"
        intended_runtime = shutil.which("stremio-runtime") or shutil.which("stremio-runtime.exe")
        intended: dict[str, object] = {
            "name": "stremio-runtime",
            "status": "present" if intended_runtime else "missing",
            "path": intended_runtime,
            "provenance": "environment" if intended_runtime else "not-found-on-PATH-or-known-install-locations",
        }
        if intended_runtime:
            intended["executable"] = _executable_identity(Path(intended_runtime).resolve())
        return {
            "oracle": {
                "path": str(self.server_js),
                "bytes": len(source_bytes),
                "sha256": hashlib.sha256(source_bytes).hexdigest(),
                "expected_bytes": ORACLE_BYTES,
                "expected_sha256": ORACLE_SHA256,
                "identity_match": len(source_bytes) == ORACLE_BYTES
                and hashlib.sha256(source_bytes).hexdigest() == ORACLE_SHA256,
            },
            "embedded": _metadata(source_bytes),
            "runtime": {
                "kind": runtime_kind,
                "path": str(self.runtime),
                "version": _version(self.runtime),
                "executable": _executable_identity(self.runtime),
                "command": [str(self.runtime), str(self.server_js)],
                "intended_stremio_runtime": intended,
            },
            "platform": {
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
                "python": platform.python_version(),
            },
            "launch_flags": {
                "probe_host": "127.0.0.1",
                "reference_listener_bind": "0.0.0.0 (oracle source; not modified)",
                "http_port_range": [self.port_start, self.port_end],
                "https_disabled": True,
                "network_interfaces_disabled": True,
            },
        }

    def companions(
        self,
        *,
        environment: Mapping[str, str] | None = None,
        search_roots: Sequence[Path] = (),
    ) -> dict[str, object]:
        env = dict(os.environ if environment is None else environment)
        report: dict[str, object] = {"repair": "none"}
        for name in ("ffmpeg", "ffprobe"):
            candidates = _candidate_paths(name, self.runtime, env, search_roots)
            resolved = next((candidate for candidate in candidates if candidate.is_file()), None)
            entry: dict[str, object] = {
                "status": "present" if resolved else "missing",
                "candidates": [str(candidate) for candidate in candidates],
                "selected": str(resolved) if resolved else None,
                "provenance": "environment" if resolved else "environment-missing",
            }
            if resolved:
                entry["executable"] = _executable_identity(resolved)
            report[name] = entry
        return report

    def _command(self, app_path: Path, settings_path: Path) -> tuple[list[str], dict[str, str]]:
        env = os.environ.copy()
        env.update(
            {
                "APP_PATH": str(app_path),
                "SETTINGS_PATH": str(settings_path),
                "NO_HTTPS_SERVER": "1",
                "NO_NETWORK_INTERFACES": "1",
                "CASTING_DISABLED": "1",
                "DISABLE_CACHING": "1",
                "HLS_V2_DISABLED": "1",
            }
        )
        return [str(self.runtime), str(self.server_js)], env

    @staticmethod
    def _request(port: int, path: str) -> dict[str, object]:
        try:
            with urllib.request.urlopen(
                urllib.request.Request(
                    f"http://127.0.0.1:{port}{path}",
                    headers={"Host": f"127.0.0.1:{port}"},
                ),
                timeout=0.5,
            ) as response:
                body = response.read()
                decoded: object = body.decode("utf-8")
                try:
                    decoded = json.loads(decoded)
                except json.JSONDecodeError:
                    pass
                return {
                    "status": response.status,
                    "headers": dict(response.headers.items()),
                    "body": body.decode("utf-8"),
                    "json": decoded,
                }
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", errors="replace")
            return {"status": error.code, "headers": dict(error.headers.items()), "body": body}
        except (urllib.error.URLError, TimeoutError, ConnectionError, socket.timeout) as error:
            return {"error": str(error)}

    @staticmethod
    def _stop(process: subprocess.Popen[str]) -> dict[str, object]:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        return {"exit": process.returncode, "alive_after": process.poll() is None}

    def run_probe(
        self,
        paths: Sequence[str],
        *,
        timeout: float = 6.0,
    ) -> dict[str, object]:
        before = _sha256(self.server_js)
        with tempfile.TemporaryDirectory(prefix="colosseum-server1-p02-") as root_name:
            root = Path(root_name)
            app_path = root / "app"
            settings_path = root / "settings"
            app_path.mkdir()
            settings_path.mkdir()
            (settings_path / "server-settings.json").write_text("{}\n", encoding="utf-8")
            command, env = self._command(app_path, settings_path)
            process = subprocess.Popen(
                command,
                cwd=str(self.server_js.parent),
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
            )
            port: int | None = None
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and process.poll() is None:
                for candidate in range(self.port_start, self.port_end + 1):
                    response = self._request(candidate, "/heartbeat")
                    if response.get("status") == 200:
                        port = candidate
                        break
                if port is not None:
                    break
                time.sleep(0.05)
            teardown = self._stop(process)
            transcript = process.stdout.read() if process.stdout else ""
            if process.stdout:
                process.stdout.close()
            responses: dict[str, object] = {}
            if port is not None:
                for path in paths:
                    responses[path] = self._request(port, path)
            failure: dict[str, object] | None = None
            if port is None:
                lower_transcript = transcript.lower()
                if "eaddrinuse" in lower_transcript or "address already in use" in lower_transcript:
                    kind = "port-exhausted"
                elif "eacces" in lower_transcript or "access is denied" in lower_transcript or "permission denied" in lower_transcript:
                    kind = "bind-denied"
                else:
                    kind = "startup-failed"
                attempted = sorted(
                    {int(value) for value in re.findall(r"(?:port|address|127\.0\.0\.1:)(11?4\d{2})", transcript) if 11470 <= int(value) <= 11475}
                )
                failure = {
                    "kind": kind,
                    "raw": transcript,
                    "attempted_ports": attempted or list(range(self.port_start, self.port_end + 1)),
                }
            return {
                "ready": port is not None,
                "port": port,
                "responses": responses,
                "paths": {"app": str(app_path), "settings": str(settings_path)},
                "command": command,
                "environment": {
                    key: env[key]
                    for key in (
                        "APP_PATH",
                        "SETTINGS_PATH",
                        "NO_HTTPS_SERVER",
                        "NO_NETWORK_INTERFACES",
                        "CASTING_DISABLED",
                        "DISABLE_CACHING",
                        "HLS_V2_DISABLED",
                    )
                },
                "transcript": transcript,
                "teardown": teardown,
                "oracle_before": before,
                "oracle_after": _sha256(self.server_js),
                "failure": failure,
            }

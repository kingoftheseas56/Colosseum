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
PORTABLE_NODE_VERSION = "v22.16.0"
PORTABLE_NODE_ARCHIVE_SHA256 = "f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e"
PORTABLE_NODE_ARCHIVE_URL = (
    "https://nodejs.org/dist/v22.16.0/node-v22.16.0-linux-x64.tar.xz"
)
PORTABLE_NODE_SHASUMS_URL = "https://nodejs.org/dist/v22.16.0/SHASUMS256.txt"


_WSL_QUALIFIER = r'''
import hashlib
import ipaddress
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

oracle = Path(sys.argv[1])
node_version = sys.argv[2]
archive_sha_expected = sys.argv[3]
archive_url = sys.argv[4]
shasums_url = sys.argv[5]
ports = list(range(11470, 11475))


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, destination):
    digest = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response, open(destination, "wb") as stream:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            stream.write(block)
            digest.update(block)
    return digest.hexdigest()


def command_version(path):
    completed = subprocess.run([path, "-version"], check=False, capture_output=True, text=True)
    output = (completed.stdout or completed.stderr).splitlines()
    return output[0] if output else ""


def companion(name):
    path = shutil.which(name)
    if not path:
        return {"status": "missing", "path": None, "repair": "none"}
    return {
        "status": "present",
        "path": path,
        "bytes": os.path.getsize(path),
        "sha256": sha256_file(path),
        "version": command_version(path),
        "provenance": "WSL Ubuntu environment",
    }


def request(port, route):
    try:
        with urllib.request.urlopen(
            urllib.request.Request(
                "http://127.0.0.1:%d%s" % (port, route),
                headers={"Host": "127.0.0.1:%d" % port},
            ),
            timeout=0.5,
        ) as response:
            body = response.read().decode("utf-8")
            try:
                parsed = json.loads(body)
            except json.JSONDecodeError:
                parsed = body
            return {
                "status": response.status,
                "headers": dict(response.headers.items()),
                "body": body,
                "json": parsed,
            }
    except (urllib.error.URLError, TimeoutError, ConnectionError, socket.timeout) as error:
        return {"error": str(error)}


def stop_process(process):
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
    alive = process.poll() is None
    return {"exit": process.returncode, "alive_after": alive, "method": method}


def network_observation(pid):
    socket_inodes = set()
    fd_root = Path("/proc") / str(pid) / "fd"
    if fd_root.is_dir():
        for fd in fd_root.iterdir():
            try:
                target = os.readlink(fd)
            except OSError:
                continue
            matched = re.fullmatch(r"socket:\[(\d+)\]", target)
            if matched:
                socket_inodes.add(matched.group(1))

    def decode_endpoint(value, ipv6):
        host_hex, port_hex = value.split(":")
        raw = bytes.fromhex(host_hex)
        if ipv6:
            raw = b"".join(raw[index:index + 4][::-1] for index in range(0, 16, 4))
        else:
            raw = raw[::-1]
        address = ipaddress.ip_address(raw)
        return str(address), int(port_hex, 16), bool(
            address.is_loopback
            or (getattr(address, "ipv4_mapped", None) and address.ipv4_mapped.is_loopback)
        )

    owned = []
    for table_name in ("tcp", "tcp6", "udp", "udp6"):
        table = Path("/proc") / str(pid) / "net" / table_name
        if not table.is_file():
            continue
        for line in table.read_text(encoding="ascii").splitlines()[1:]:
            fields = line.split()
            if len(fields) < 10 or fields[9] not in socket_inodes:
                continue
            ipv6 = table_name.endswith("6")
            local_host, local_port, _ = decode_endpoint(fields[1], ipv6)
            remote_host, remote_port, remote_loopback = decode_endpoint(fields[2], ipv6)
            owned.append({
                "protocol": table_name,
                "local": "%s:%d" % (local_host, local_port),
                "remote": "%s:%d" % (remote_host, remote_port),
                "remote_loopback": remote_loopback,
                "state": fields[3],
                "inode": fields[9],
            })
    unexpected = [
        row for row in owned
        if not row["remote_loopback"] and not row["remote"].endswith(":0")
    ]
    return {
        "method": "process fd socket inode correlation against /proc/<pid>/net",
        "owned_sockets": owned,
        "unexpected_outbound": unexpected,
    }


def run_server(node, root, occupy_all):
    lane = "occupied" if occupy_all else "endpoints"
    case_root = root / lane
    app_path = case_root / "app"
    settings_path = case_root / "settings"
    app_path.mkdir(parents=True)
    settings_path.mkdir(parents=True)
    (settings_path / "server-settings.json").write_text("{}\n", encoding="utf-8")

    occupied = []
    if occupy_all:
        for port in ports:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("127.0.0.1", port))
            sock.listen(1)
            occupied.append(sock)

    env = os.environ.copy()
    env.update({
        "APP_PATH": str(app_path),
        "SETTINGS_PATH": str(settings_path),
        "NO_HTTPS_SERVER": "1",
        "NO_NETWORK_INTERFACES": "1",
        "CASTING_DISABLED": "1",
        "DISABLE_CACHING": "1",
        "HLS_V2_DISABLED": "1",
        "FFMPEG_BIN": "/usr/bin/ffmpeg",
        "FFPROBE_BIN": "/usr/bin/ffprobe",
    })
    log_path = case_root / "server.log"
    log_stream = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(node), str(oracle)],
        cwd=str(oracle.parent),
        env=env,
        stdout=log_stream,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    ready_port = None
    responses = {}
    attempted = []
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline and process.poll() is None:
        if occupy_all:
            log_stream.flush()
            transcript_so_far = log_path.read_text(encoding="utf-8", errors="replace")
            attempted = sorted({
                int(value)
                for value in re.findall(r"port:\s*(1147[0-5])", transcript_so_far)
            })
            if attempted == ports:
                break
        else:
            for port in ports:
                heartbeat = request(port, "/heartbeat")
                if heartbeat.get("status") == 200:
                    ready_port = port
                    responses["/heartbeat"] = heartbeat
                    responses["/settings"] = request(port, "/settings")
                    break
            if ready_port is not None:
                break
        time.sleep(0.05)

    network = network_observation(process.pid)
    teardown = stop_process(process)
    log_stream.close()
    transcript = log_path.read_text(encoding="utf-8", errors="replace")
    for sock in occupied:
        sock.close()

    attempted = sorted({
        int(value)
        for value in re.findall(r"port:\s*(1147[0-5])", transcript)
    })
    result = {
        "command": [str(node), str(oracle)],
        "environment": {key: env[key] for key in (
            "APP_PATH", "SETTINGS_PATH", "NO_HTTPS_SERVER",
            "NO_NETWORK_INTERFACES", "CASTING_DISABLED", "DISABLE_CACHING",
            "HLS_V2_DISABLED", "FFMPEG_BIN", "FFPROBE_BIN",
        )},
        "port": ready_port,
        "responses": responses,
        "transcript": transcript,
        "network_observation": network,
        "teardown": teardown,
        "oracle_before": oracle_hash_before,
        "oracle_after": sha256_file(oracle),
        "paths": {"app": str(app_path), "settings": str(settings_path)},
    }
    if occupy_all:
        result.update({
            "state": "PASS" if ready_port is None and attempted == ports else "FAIL",
            "occupied_ports": ports,
            "failure": {"kind": "port-exhausted", "attempted_ports": attempted},
        })
    else:
        endpoint_ok = (
            ready_port == 11470
            and responses.get("/heartbeat", {}).get("status") == 200
            and responses.get("/settings", {}).get("status") == 200
        )
        result["state"] = "PASS" if endpoint_ok else "FAIL"
    return result


oracle_hash_before = sha256_file(oracle)
temp_root = Path(tempfile.mkdtemp(prefix="colosseum-server1-p02-"))
result = None
try:
    archive_name = "node-v22.16.0-linux-x64.tar.xz"
    archive = temp_root / archive_name
    with urllib.request.urlopen(shasums_url, timeout=60) as response:
        shasums_bytes = response.read()
    shasums_text = shasums_bytes.decode("utf-8")
    checksum_line = next(
        line for line in shasums_text.splitlines() if line.endswith("  " + archive_name)
    )
    official_sha = checksum_line.split()[0]
    downloaded_sha = download(archive_url, archive)
    if official_sha != archive_sha_expected or downloaded_sha != archive_sha_expected:
        raise RuntimeError("portable Node checksum mismatch")

    extract_root = temp_root / "runtime"
    extract_root.mkdir()
    with tarfile.open(archive, "r:xz") as package:
        package.extractall(extract_root)
    node = extract_root / "node-v22.16.0-linux-x64" / "bin" / "node"
    runtime_version = subprocess.run(
        [str(node), "--version"], check=True, capture_output=True, text=True
    ).stdout.strip()

    endpoint_case = run_server(node, temp_root, False)
    occupied_case = run_server(node, temp_root, True)
    result = {
        "schema": "colosseum-server1-p02-wsl-qualification/v1",
        "platform": {
            "distro": "Ubuntu",
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "oracle": {
            "path": str(oracle),
            "bytes": os.path.getsize(oracle),
            "sha256": oracle_hash_before,
        },
        "runtime": {
            "kind": "portable-node",
            "version": runtime_version,
            "path": str(node),
            "bytes": os.path.getsize(node),
            "sha256": sha256_file(node),
            "source_url": archive_url,
            "shasums_url": shasums_url,
            "shasums_sha256": hashlib.sha256(shasums_bytes).hexdigest(),
            "official_checksum_line": checksum_line,
            "archive_sha256": downloaded_sha,
            "official_checksum_match": True,
            "installed": False,
            "disposable_root": str(temp_root),
        },
        "companions": {
            "ffmpeg": companion("ffmpeg"),
            "ffprobe": companion("ffprobe"),
            "stremio-runtime": {
                "status": "missing" if shutil.which("stremio-runtime") is None else "present",
                "path": shutil.which("stremio-runtime"),
                "provenance": "WSL Ubuntu environment",
                "repair": "none",
            },
        },
        "cases": {
            "P02-01-WSL": endpoint_case,
            "P02-02-WSL": occupied_case,
        },
    }
finally:
    shutil.rmtree(temp_root, ignore_errors=False)

result["runtime"]["disposable_storage_removed"] = not temp_root.exists()
for case in result["cases"].values():
    case["paths_removed"] = not temp_root.exists()
print(json.dumps(result, sort_keys=True))
'''


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

    def qualify_wsl(self, distro: str = "Ubuntu") -> dict[str, object]:
        """Qualify the exact oracle under a verified disposable Linux Node runtime."""
        oracle_hash = _sha256(self.server_js)
        if self.server_js.stat().st_size != ORACLE_BYTES or oracle_hash != ORACLE_SHA256:
            raise ReferenceRuntimeError("refusing WSL launch: oracle identity mismatch")
        drive, tail = os.path.splitdrive(str(self.server_js))
        if not drive:
            raise ReferenceRuntimeError("WSL qualification requires a drive-qualified oracle path")
        normalized_tail = tail.lstrip("\\/").replace("\\", "/")
        wsl_oracle = f"/mnt/{drive[0].lower()}/{normalized_tail}"
        completed = subprocess.run(
            [
                "wsl.exe",
                "-d",
                distro,
                "--",
                "python3",
                "-",
                wsl_oracle,
                PORTABLE_NODE_VERSION,
                PORTABLE_NODE_ARCHIVE_SHA256,
                PORTABLE_NODE_ARCHIVE_URL,
                PORTABLE_NODE_SHASUMS_URL,
            ],
            input=_WSL_QUALIFIER,
            check=False,
            capture_output=True,
            text=True,
            timeout=240,
        )
        if completed.returncode != 0:
            raise ReferenceRuntimeError(
                f"WSL qualification failed ({completed.returncode}): {completed.stderr.strip()}"
            )
        try:
            return json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise ReferenceRuntimeError(
                f"WSL qualification returned invalid JSON: {completed.stdout[-1000:]}"
            ) from error

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
            responses: dict[str, object] = {}
            if port is not None:
                for path in paths:
                    responses[path] = self._request(port, path)
            teardown = self._stop(process)
            transcript = process.stdout.read() if process.stdout else ""
            if process.stdout:
                process.stdout.close()
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

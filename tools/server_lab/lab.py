"""Standalone P04 laboratory CLI and LabRunner interface."""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .evidence import EvidenceSchema
from .scenarios import classify, expected_response


class LabRunner:
    def run(self, *, subject: Path, mode: str, data_root: Path, evidence_dir: Path, run_id: str, config_path: Path | None = None) -> dict[str, Any]:
        data_root.mkdir(parents=True, exist_ok=True)
        startup_cleanup = cleanup_orphans(data_root)
        run_root = data_root / run_id
        cache = run_root / "cache"
        event_file = run_root / "events.jsonl"
        run_root.mkdir(parents=True, exist_ok=False)
        cache.mkdir()
        evidence_dir.mkdir(parents=True, exist_ok=True)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(4)
        host, port = listener.getsockname()
        endpoint = f"{host}:{port}"
        started = datetime.now(timezone.utc).isoformat()
        token = uuid.uuid4().hex
        command = [sys.executable, str(subject), mode, token]
        stdout_path = run_root / "stdout.txt"
        stderr_path = run_root / "stderr.txt"
        lease_path = run_root / "ownership.json"
        timed_out = False
        exit_code: int | None = None
        process: subprocess.Popen[str] | None = None
        lease: dict[str, Any] | None = None
        termination_errors: list[str] = []
        try:
            environment = {**os.environ, "LAB_RUN_ROOT": str(run_root), "LAB_EVENT_FILE": str(event_file), "LAB_PORT": str(port), "LAB_OWNERSHIP_TOKEN": token}
            popen_options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {"start_new_session": True}
            with stdout_path.open("w", encoding="utf-8") as stdout_stream, stderr_path.open("w", encoding="utf-8") as stderr_stream:
                process = subprocess.Popen(command, cwd=run_root, env=environment, stdout=stdout_stream, stderr=stderr_stream, text=True, **popen_options)
                lease = {"token": token, "pid": process.pid, "command": command, "started_at": started, "controller": {"pid": os.getpid(), "identity": {"creation": _process_creation(os.getpid())}}, "identity": {"subject": str(subject), "mode": mode, "token": token, "command": subprocess.list2cmdline(command), "creation": _process_creation(process.pid), "pending": False}}
                lease_path.write_text(json.dumps(lease, indent=2) + "\n", encoding="utf-8")
                try:
                    exit_code = process.wait(timeout=30.0 if mode == "orphan" else (3.0 if mode == "hold" else 1.0))
                except subprocess.TimeoutExpired:
                    timed_out = True
                    _terminate_tree(process.pid)
                    exit_code = process.wait(timeout=2.0)
        finally:
            if process and process.poll() is None:
                _terminate_tree(process.pid)
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    termination_errors.append(f"owned process did not exit: {process.pid}")
            if process:
                termination_errors.extend(_wait_for_release(run_root, process.pid, (stdout_path, stderr_path)))
            listener.close()
            if process and process.poll() is not None:
                lease_path.unlink(missing_ok=True)
        response_path = run_root / "subject-response.json"
        response = json.loads(response_path.read_text(encoding="utf-8")) if response_path.is_file() else None
        result, errors = classify(mode, response, timed_out=timed_out, exit_code=exit_code)
        protocol_path = run_root / "protocol-response.txt"
        protocol_bytes = protocol_path.read_bytes() if protocol_path.is_file() else b""
        protocol_text = protocol_bytes.decode("latin-1")
        protocol_status, protocol_headers = _parse_protocol(protocol_text)
        owned_children_after: list[int] = []
        cleanup_errors: list[str] = []
        for marker in ("descendant.pid", "grandchild.pid"):
            marker_path = run_root / marker
            if marker_path.is_file():
                try:
                    marker_pid = int(marker_path.read_text(encoding="ascii"))
                    if _process_identity(marker_pid):
                        owned_children_after.append(marker_pid)
                except (OSError, ValueError):
                    cleanup_errors.append(f"invalid owned child marker: {marker}")
        if lease_path.exists():
            try:
                owned_pid = int(json.loads(lease_path.read_text(encoding="utf-8"))["pid"])
                if _process_identity(owned_pid):
                    owned_children_after.append(owned_pid)
            except (OSError, ValueError, KeyError, TypeError):
                cleanup_errors.append("unresolved ownership lease")
        raw = {"protocol_text": protocol_text, "protocol_bytes_hex": protocol_bytes.hex(), "protocol_status": protocol_status, "protocol_headers": protocol_headers, "stdout": stdout_path.read_text(encoding="utf-8", errors="replace"), "stderr": stderr_path.read_text(encoding="utf-8", errors="replace"), "exit_code": exit_code}
        normalized = {"response": response, "expected": expected_response(mode)}
        raw_body = protocol_text.split("\r\n\r\n", 1)[1] if "\r\n\r\n" in protocol_text else ""
        if response is not None and (protocol_status != response.get("status") or protocol_headers.get("Content-Type") != response.get("headers", {}).get("Content-Type") or raw_body != response.get("body")):
            result = "FAIL"
            errors.append("raw protocol divergence")
        errors.extend(cleanup_errors)
        errors.extend(termination_errors)
        if owned_children_after or cleanup_errors or termination_errors:
            result = "ERROR"
            errors.append("owned process cleanup unresolved")
        event_file.write_text(json.dumps({"run_id": run_id, "result": result}) + "\n", encoding="utf-8")
        timeout_seconds = 30.0 if mode == "orphan" else (3.0 if mode == "hold" else 1.0)
        replay_command = [sys.executable, "-m", "tools.server_lab.lab"]
        if config_path:
            replay_command += ["--config", str(config_path)]
        replay_command += ["--subject", str(subject), "--mode", mode, "--data-root", str(data_root), "--evidence-dir", str(evidence_dir), "--run-id", run_id]
        replay_template = ["{python}", "-m", "tools.server_lab.lab"]
        if config_path:
            replay_template += ["--config", "{config}"]
        replay_template += ["--subject", "{subject}", "--mode", mode, "--data-root", "{data_root}", "--evidence-dir", "{evidence_dir}", "--run-id", "{run_id}"]
        required_substitutions = ["python"]
        if config_path:
            required_substitutions.append("config")
        required_substitutions += ["subject", "data_root", "evidence_dir", "run_id"]
        receipt = EvidenceSchema.receipt(result=result, run_id=run_id, engine={"name": "python-subprocess-lab", "version": sys.version.split()[0]}, source={"identity": str(subject), "sha256": _sha256(subject)}, scenario=mode, environment={"platform": sys.platform}, configuration={"timeout_seconds": timeout_seconds, "endpoint": endpoint}, fixture_identifiers={"subject_mode": mode}, request_sequence=[{"operation": "launch", "argv": command}], timestamps={"started": started, "finished": datetime.now(timezone.utc).isoformat()}, raw_lane=raw, normalized_lane=normalized, responses=[response] if response is not None else [], byte_counts=[len(protocol_bytes)], peer_observations=[], resource_observations=[], observations=[{"listener_owned": (run_root / "listener-connected").is_file(), "endpoint": endpoint}], errors=errors, replay={"exact_command": replay_command, "command_template": replay_template, "required_substitutions": required_substitutions, "configuration": {"mode": mode, "endpoint": endpoint}}, paths={"run_root": str(run_root), "cache": str(cache), "event_file": str(event_file), "endpoint": endpoint, "port": port, "stdout": str(stdout_path), "stderr": str(stderr_path)}, cleanup={"owned_children_after": owned_children_after, "errors": cleanup_errors, "orphan_detected": bool(lease_path.exists())}, startup_cleanup=startup_cleanup)
        EvidenceSchema.write(evidence_dir / "run.json", receipt)
        return receipt


def cleanup_orphans(data_root: Path) -> dict[str, Any]:
    removed: list[int] = []
    verified: list[int] = []
    skipped: list[str] = []
    active: list[str] = []
    stale: list[str] = []
    orphaned_runs: list[str] = []
    for lease_path in data_root.glob("*/ownership.json"):
        try:
            lease = json.loads(lease_path.read_text(encoding="utf-8"))
            pid = int(lease["pid"])
            expected = lease["identity"]
        except (OSError, ValueError, KeyError, TypeError):
            skipped.append(str(lease_path.parent))
            continue
        current = _process_identity(pid)
        controller = lease.get("controller", {})
        controller_pid = int(controller.get("pid", 0) or 0)
        controller_identity = controller.get("identity") or {}
        current_controller = {"creation": _process_creation(controller_pid)} if controller_pid else None
        if current_controller and controller_identity.get("creation") == current_controller.get("creation"):
            active.append(str(lease_path.parent))
            continue
        if current is None:
            stale.append(str(lease_path.parent))
            lease_path.unlink(missing_ok=True)
            continue
        if not current or expected.get("creation") != _process_creation(pid) or expected.get("subject", "").lower() not in current.get("command", "").lower() or expected.get("mode", "").lower() not in current.get("command", "").lower() or expected.get("token", "").lower() not in current.get("command", "").lower():
            skipped.append(str(lease_path.parent))
            continue
        _terminate_tree(pid)
        if _process_identity(pid) is None:
            removed.append(pid)
            verified.append(pid)
            orphaned_runs.append(str(lease_path.parent))
            lease_path.unlink(missing_ok=True)
        else:
            skipped.append(str(lease_path.parent))
    return {"removed_pids": removed, "verified_pids": verified, "skipped_mismatches": skipped, "active_leases": active, "stale_leases": stale, "orphaned_runs": orphaned_runs}


def _terminate_tree(pid: int) -> None:
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True, check=False)
    else:
        try:
            os.killpg(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def _wait_for_release(run_root: Path, root_pid: int, paths: tuple[Path, ...], timeout: float = 2.0) -> list[str]:
    marker_paths = tuple(run_root / name for name in ("descendant.pid", "grandchild.pid"))
    deadline = time.monotonic() + timeout
    while True:
        pids = [root_pid]
        for marker in marker_paths:
            try:
                if marker.is_file():
                    pids.append(int(marker.read_text(encoding="ascii")))
            except (OSError, ValueError):
                pass
        live = sorted({pid for pid in pids if _pid_alive(pid)})
        locked = [str(path) for path in paths if _is_locked(path)]
        if not live and not locked:
            return []
        if time.monotonic() >= deadline:
            details = []
            if live:
                details.append(f"owned processes still live: {live}")
            if locked:
                details.append(f"output handles still held: {locked}")
            return details
        time.sleep(0.02)


def _is_locked(path: Path) -> bool:
    if not path.exists() or os.name != "nt":
        return False
    try:
        kernel32 = _kernel32()
        handle = kernel32.CreateFileW(str(path), 0x80000000, 0, None, 3, 0, None)
        if _invalid_handle(handle):
            return True
        kernel32.CloseHandle(handle)
        return False
    except OSError:
        return True


def _pid_alive(pid: int) -> bool:
    if os.name != "nt":
        try:
            os.kill(pid, 0)
            return True
        except ProcessLookupError:
            return False
    import ctypes
    kernel32 = _kernel32()
    handle = kernel32.OpenProcess(0x1000 | 0x0400, False, pid)
    if _invalid_handle(handle):
        return False
    code = ctypes.wintypes.DWORD()
    try:
        if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == 259
    finally:
        kernel32.CloseHandle(handle)


def _process_creation(pid: int) -> str | None:
    if os.name != "nt":
        return None
    import ctypes
    kernel32 = _kernel32()
    handle = kernel32.OpenProcess(0x1000 | 0x0400, False, pid)
    if _invalid_handle(handle):
        return None
    created = ctypes.wintypes.FILETIME()
    exited = ctypes.wintypes.FILETIME()
    kernel = ctypes.wintypes.FILETIME()
    user = ctypes.wintypes.FILETIME()
    try:
        if not kernel32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited), ctypes.byref(kernel), ctypes.byref(user)):
            return None
        value = (created.dwHighDateTime << 32) | created.dwLowDateTime
        return str(value)
    finally:
        kernel32.CloseHandle(handle)


def _kernel32():
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel32.GetExitCodeProcess.restype = wintypes.BOOL
    kernel32.GetProcessTimes.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME)]
    kernel32.GetProcessTimes.restype = wintypes.BOOL
    return kernel32


def _invalid_handle(handle) -> bool:
    import ctypes
    value = getattr(handle, "value", handle)
    return value in (None, 0, ctypes.c_void_p(-1).value)


def _process_identity(pid: int) -> dict[str, Any] | None:
    if os.name == "nt":
        command = ["powershell", "-NoProfile", "-Command", f"Get-CimInstance Win32_Process -Filter 'ProcessId = {pid}' | Select-Object ProcessId,CommandLine,CreationDate | ConvertTo-Json -Compress"]
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        if completed.returncode != 0 or not completed.stdout.strip():
            return None
        try:
            value = json.loads(completed.stdout)
        except json.JSONDecodeError:
            return None
        creation = value.get("CreationDate")
        if isinstance(creation, str) and creation.startswith("/Date(") and creation.endswith(")/"):
            creation = str(int(creation[6:-2]) * 10000 + 116444736000000000)
        return {"command": value.get("CommandLine", ""), "creation": creation}
    try:
        value = Path(f"/proc/{pid}/cmdline").read_bytes().decode(errors="replace").replace("\x00", " ").strip()
        creation = Path(f"/proc/{pid}/stat").read_text(encoding="ascii").split()[21]
    except (OSError, IndexError):
        return None
    return {"command": value, "creation": creation}


def _parse_protocol(text: str) -> tuple[int | None, dict[str, str]]:
    head = text.split("\r\n\r\n", 1)[0]
    lines = head.split("\r\n")
    parts = lines[0].split() if lines else []
    status = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else None
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            key, value = line.split(":", 1)
            headers[key] = value.strip()
    return status, headers


def _sha256(path: Path) -> str:
    import hashlib
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--subject", type=Path)
    parser.add_argument("--mode")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--run-id", default=f"run-{uuid.uuid4().hex[:12]}")
    parser.add_argument("--cleanup", action="store_true")
    args = parser.parse_args(argv)
    if args.cleanup:
        print(json.dumps(cleanup_orphans(args.data_root.resolve()), sort_keys=True))
        return 0
    config = json.loads(args.config.read_text(encoding="utf-8")) if args.config else {}
    mode = args.mode or config.get("mode")
    if not args.subject or not mode or not args.evidence_dir:
        parser.error("--subject, --mode, and --evidence-dir are required unless --cleanup is used")
    receipt = LabRunner().run(subject=args.subject.resolve(), mode=mode, data_root=args.data_root.resolve(), evidence_dir=args.evidence_dir.resolve(), run_id=args.run_id, config_path=args.config.resolve() if args.config else None)
    return 0 if receipt["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

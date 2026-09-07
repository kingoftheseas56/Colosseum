"""Pinned-player lifecycle probe and honest playback evidence receipt."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from ..evidence import EvidenceSchema


CASE_INPUTS = (
    "warm_cache",
    "player_buffer",
    "file_index",
    "peer_script",
    "resource_counter",
)
EVENT_NAMES = {
    "valid_media_decode": "valid_media_decode",
    "presented_frame": "first_presented_frame",
    "seek_complete": "seek_completion",
    "stall": "stall",
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _state(name: str, state: str, **details: Any) -> dict[str, Any]:
    return {"name": name, "state": state, **details}


class PlayerProbe:
    """Run a pinned player and preserve every observable lifecycle boundary.

    A player may publish newline-delimited JSON events to the path in
    ``COLOSSEUM_PLAYER_PROBE_EVENT_FILE``.  The probe never infers decode,
    frame, seek, stall, or resource-counter facts when the player does not
    publish them.
    """

    def run(
        self,
        *,
        executable: Path | str,
        media_url: str,
        data_root: Path,
        evidence_dir: Path,
        run_id: str,
        config: Path | str | None = None,
        case_inputs: dict[str, Any] | None = None,
        seek_seconds: float | None = None,
        timeout_seconds: float = 5.0,
        http_ready: bool = True,
        runner: Any | None = None,
        fixture_corpus: Any | None = None,
        cancel_requested: Callable[[], bool] | None = None,
        reference_runtime: Any | None = None,
        fixture_identifiers: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        started_at = _utc_now()
        data_root = data_root.resolve()
        evidence_dir = evidence_dir.resolve()
        executable_path = self._resolve_executable(Path(executable))
        inputs = self._case_inputs(case_inputs)
        run_root = data_root / run_id
        run_root.mkdir(parents=True, exist_ok=False)
        evidence_dir.mkdir(parents=True, exist_ok=True)
        event_file = run_root / "player-events.jsonl"
        stdout_path = run_root / "stdout.txt"
        stderr_path = run_root / "stderr.txt"
        config_record = self._config_record(config)
        version = self._version(executable_path)
        command = self._command(executable_path, media_url, seek_seconds)
        environment = {
            **os.environ,
            "COLOSSEUM_PLAYER_PROBE_EVENT_FILE": str(event_file),
            "COLOSSEUM_PLAYER_PROBE_RUN_ROOT": str(run_root),
            "COLOSSEUM_PLAYER_PROBE_MEDIA_URL": media_url,
        }
        observations: dict[str, dict[str, Any]] = {
            "process_start": _state("process_start", "PASS" if version["available"] else "ERROR"),
            "http_readiness": _state("http_readiness", "NOT_RUN"),
            "valid_media_decode": _state("valid_media_decode", "UNAVAILABLE", reason="no player telemetry yet"),
            "first_presented_frame": _state("first_presented_frame", "UNAVAILABLE", reason="no player telemetry yet"),
            "seek_completion": _state("seek_completion", "UNAVAILABLE", reason="no player telemetry yet"),
            "stall": _state("stall", "UNAVAILABLE", reason="no player telemetry yet"),
            "timeout": _state("timeout", "NOT_RUN"),
            "cancellation": _state("cancellation", "NOT_RUN"),
            "unavailable_telemetry": _state("unavailable_telemetry", "UNAVAILABLE", reason="player event channel has not reported all observations"),
        }
        if http_ready:
            observations["http_readiness"] = self._http_readiness(media_url, timeout_seconds)
        else:
            observations["http_readiness"] = _state("http_readiness", "UNAVAILABLE", reason="readiness check not configured")

        process: subprocess.Popen[str] | None = None
        events: list[dict[str, Any]] = []
        timed_out = False
        cancelled = False
        exit_code: int | None = None
        launch_error: str | None = None
        if version["available"]:
            try:
                with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
                    process = subprocess.Popen(command, cwd=run_root, env=environment, stdout=stdout, stderr=stderr, text=True)
                    deadline = time.monotonic() + timeout_seconds
                    while process.poll() is None:
                        events = self._read_events(event_file)
                        self._apply_events(observations, events)
                        if cancel_requested and cancel_requested():
                            cancelled = True
                            observations["cancellation"] = _state("cancellation", "PASS", reason="requested by caller")
                            process.terminate()
                            break
                        if time.monotonic() >= deadline:
                            timed_out = True
                            observations["timeout"] = _state("timeout", "PASS", limit_seconds=timeout_seconds)
                            process.terminate()
                            break
                        time.sleep(0.01)
                    try:
                        exit_code = process.wait(timeout=1.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        exit_code = process.wait(timeout=1.0)
                    events = self._read_events(event_file)
                    self._apply_events(observations, events)
            except OSError as error:
                launch_error = str(error)
                observations["process_start"] = _state("process_start", "ERROR", error=launch_error)
        else:
            launch_error = version.get("error") or "player version unavailable"

        if timed_out:
            result = "ERROR"
            errors = ["player probe timed out"]
        elif cancelled:
            result = "ERROR"
            errors = ["player probe cancelled"]
        elif launch_error:
            result = "ERROR"
            errors = [launch_error]
        elif exit_code not in (0, None):
            result = "ERROR"
            errors = [f"player exited with {exit_code}"]
        elif all(observations[key]["state"] == "PASS" for key in ("valid_media_decode", "first_presented_frame", "seek_completion")):
            result = "PASS"
            errors = []
        else:
            result = "INDETERMINATE"
            errors = ["required player telemetry is unavailable"]

        resource_state = "PASS" if inputs["resource_counter"] is not None else "UNAVAILABLE"
        resource_observations = [{"name": "resource_counter", "state": resource_state, "value": inputs["resource_counter"]}]
        unavailable = [name for name, value in observations.items() if name != "unavailable_telemetry" and value["state"] == "UNAVAILABLE"]
        observations["unavailable_telemetry"] = _state("unavailable_telemetry", "PASS" if not unavailable else "UNAVAILABLE", fields=unavailable)
        raw_stdout = stdout_path.read_text(encoding="utf-8", errors="replace") if stdout_path.exists() else ""
        raw_stderr = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
        receipt = EvidenceSchema.receipt(
            result=result,
            run_id=run_id,
            engine={"name": "pinned-player-probe", "version": "1"},
            source={"identity": str(executable_path), "sha256": _sha256(executable_path), "authority": "P07-B"},
            scenario="P07-02",
            environment={"platform": sys.platform, "python": sys.version.split()[0]},
            configuration={
                "player_executable": str(executable_path),
                "player_sha256": _sha256(executable_path),
                "player_version": version,
                "config": config_record,
                "media_url": media_url,
                "seek_seconds": seek_seconds,
                "timeout_seconds": timeout_seconds,
                "command": command,
                "interfaces_consumed": {
                    "LabRunner": self._interface_record(runner),
                    "QualifiedFixtureCorpus": self._interface_record(fixture_corpus),
                    "QualifiedReferenceRuntime": self._interface_record(reference_runtime),
                },
            },
            fixture_identifiers={**(fixture_identifiers or {}), "case_inputs": inputs},
            request_sequence=[{"operation": "version", "argv": self._version_command(executable_path)}, {"operation": "launch", "argv": command}],
            timestamps={"started": started_at, "finished": _utc_now()},
            raw_lane={"events": events, "stdout": raw_stdout, "stderr": raw_stderr, "exit_code": exit_code, "launch_error": launch_error},
            normalized_lane={"observations": observations},
            observations=list(observations.values()),
            errors=errors,
            replay={"exact_command": command, "event_file": str(event_file), "required_substitutions": ["executable", "media_url", "data_root", "evidence_dir", "run_id"]},
            resource_observations=resource_observations,
            peer_observations=[{"name": "peer_script", "state": "DECLARED", "value": inputs["peer_script"]}],
            paths={"run_root": str(run_root), "event_file": str(event_file), "stdout": str(stdout_path), "stderr": str(stderr_path), "config": config_record.get("path")},
        )
        EvidenceSchema.write(evidence_dir / "run.json", receipt)
        return receipt

    @staticmethod
    def _case_inputs(values: dict[str, Any] | None) -> dict[str, Any]:
        supplied = values or {}
        missing = [key for key in CASE_INPUTS if key not in supplied]
        if missing:
            raise ValueError(f"P07-02 case inputs missing: {missing}")
        return {key: supplied[key] for key in CASE_INPUTS}

    @staticmethod
    def _resolve_executable(value: Path) -> Path:
        resolved = Path(shutil.which(str(value)) or value).expanduser().resolve()
        if not resolved.is_file():
            raise FileNotFoundError(f"player executable not found: {value}")
        return resolved

    @staticmethod
    def _version_command(executable: Path) -> list[str]:
        return ([sys.executable, str(executable)] if executable.suffix.lower() == ".py" else [str(executable)]) + ["--version"]

    @classmethod
    def _version(cls, executable: Path) -> dict[str, Any]:
        command = cls._version_command(executable)
        try:
            completed = subprocess.run(command, capture_output=True, text=True, check=False, timeout=5)
        except (OSError, subprocess.TimeoutExpired) as error:
            return {"available": False, "command": command, "exit_code": None, "stdout": "", "stderr": str(error), "error": str(error)}
        return {"available": completed.returncode == 0 and bool((completed.stdout + completed.stderr).strip()), "command": command, "exit_code": completed.returncode, "stdout": completed.stdout, "stderr": completed.stderr, "text": (completed.stdout + completed.stderr).strip()}

    @staticmethod
    def _command(executable: Path, media_url: str, seek_seconds: float | None) -> list[str]:
        command = [sys.executable, str(executable)] if executable.suffix.lower() == ".py" else [str(executable)]
        command += [media_url]
        if seek_seconds is not None:
            command += ["--start", str(seek_seconds)]
        return command

    @staticmethod
    def _config_record(config: Path | str | None) -> dict[str, Any]:
        if config is None:
            return {"state": "UNAVAILABLE", "reason": "no config path supplied"}
        path = Path(config).expanduser().resolve()
        if not path.is_file():
            return {"state": "UNAVAILABLE", "path": str(path), "reason": "config path is not a file"}
        return {"state": "PASS", "path": str(path), "sha256": _sha256(path), "bytes": path.stat().st_size}

    @staticmethod
    def _interface_record(value: Any | None) -> dict[str, Any]:
        if value is None:
            return {"state": "UNAVAILABLE"}
        return {"state": "AVAILABLE", "type": f"{type(value).__module__}.{type(value).__qualname__}"}

    @staticmethod
    def _http_readiness(url: str, timeout: float) -> dict[str, Any]:
        started = time.monotonic()
        try:
            request = urllib.request.Request(url, method="HEAD")
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return _state("http_readiness", "PASS", status=response.status, elapsed_ms=round((time.monotonic() - started) * 1000, 3))
        except (OSError, urllib.error.URLError, urllib.error.HTTPError) as error:
            return _state("http_readiness", "UNAVAILABLE", reason=str(error), elapsed_ms=round((time.monotonic() - started) * 1000, 3))

    @staticmethod
    def _read_events(path: Path) -> list[dict[str, Any]]:
        if not path.is_file():
            return []
        events: list[dict[str, Any]] = []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict) and isinstance(value.get("event"), str):
                events.append(value)
        return events

    @staticmethod
    def _apply_events(observations: dict[str, dict[str, Any]], events: list[dict[str, Any]]) -> None:
        for event in events:
            name = EVENT_NAMES.get(event.get("event"))
            if not name:
                continue
            details = {key: value for key, value in event.items() if key != "event"}
            observations[name] = _state(name, "PASS", **details)

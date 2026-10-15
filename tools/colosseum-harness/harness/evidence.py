from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any

from .repo import HarnessError

def load_lanista_session_manifest(path: Path | str) -> tuple[dict[str, Any], Path]:
    manifest_path = Path(path).expanduser().resolve()
    try:
        raw = manifest_path.read_bytes()
        value = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Cannot read Lanista session manifest: {manifest_path}",
        ) from exc
    if not isinstance(value, dict) or value.get("schema") != "colosseum.session.v1":
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Not a Colosseum Lanista v1 session manifest: {manifest_path}",
        )

    required_strings = (
        "sessionId", "tag", "pipe", "exe", "exeSha256", "appDataRoot", "cacheRoot",
    )
    for key in required_strings:
        if not isinstance(value.get(key), str) or not value[key].strip():
            raise HarnessError(
                "LANISTA_SESSION_INVALID",
                f"Lanista session manifest has no valid {key}: {manifest_path}",
            )
    pid = value.get("pid")
    valid_pid = (
        isinstance(pid, int) and not isinstance(pid, bool) and pid > 0
    ) or (
        isinstance(pid, float) and pid.is_integer() and pid > 0
    )
    if not valid_pid:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session manifest has no valid pid: {manifest_path}",
        )
    if not re.fullmatch(r"[0-9a-fA-F]{64}", value["exeSha256"]):
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session manifest has no valid exeSha256: {manifest_path}",
        )
    if value["pipe"] != f"ColosseumLanista-{value['sessionId']}":
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session pipe does not match its session id: {manifest_path}",
        )
    marker = f"Colosseum-dltest-{value['tag']}"
    if marker not in value["appDataRoot"] or marker not in value["cacheRoot"]:
        raise HarnessError(
            "LANISTA_SESSION_INVALID",
            f"Lanista session isolation roots do not match its tag: {manifest_path}",
        )
    return value, manifest_path

def _persisted_execution_result(result: dict[str, Any]) -> dict[str, Any]:
    persisted = {
        key: result[key]
        for key in (
            "selector",
            "kind",
            "name",
            "path",
            "mode",
            "selectedTests",
            "argv",
            "exitCode",
        )
        if key in result
    }
    for stream in ("stdout", "stderr"):
        value = result.get(stream, "")
        if not isinstance(value, str):
            value = str(value)
        encoded = value.encode("utf-8", errors="replace")
        persisted[f"{stream}Bytes"] = len(encoded)
        persisted[f"{stream}Sha256"] = hashlib.sha256(encoded).hexdigest()
        persisted[f"{stream}Tail"] = value[-4000:]
    return persisted

def _completion_blocker(code: str, message: str, **details: Any) -> dict[str, Any]:
    blocker: dict[str, Any] = {"code": code, "message": message}
    if details:
        blocker["details"] = details
    return blocker

def run_completion_blockers(receipt: dict[str, Any]) -> list[dict[str, Any]]:
    blockers: list[dict[str, Any]] = []

    task = receipt.get("task")
    paths = receipt.get("paths")
    source = receipt.get("source")
    source_valid = (
        isinstance(task, str)
        and bool(task.strip())
        and isinstance(paths, list)
        and bool(paths)
        and all(isinstance(path, str) and bool(path.strip()) for path in paths)
        and isinstance(source, list)
        and len(source) == len(paths)
    )
    if source_valid:
        for expected_path, item in zip(paths, source):
            if not isinstance(item, dict) or item.get("path") != expected_path:
                source_valid = False
                break
            exists = item.get("exists")
            if not isinstance(exists, bool):
                source_valid = False
                break
            if exists:
                sha256 = item.get("sha256")
                size = item.get("sizeBytes")
                if (
                    not isinstance(sha256, str)
                    or not re.fullmatch(r"[0-9a-f]{64}", sha256)
                    or isinstance(size, bool)
                    or not isinstance(size, int)
                    or size < 0
                ):
                    source_valid = False
                    break
            elif item.get("sha256") is not None or item.get("sizeBytes") is not None:
                source_valid = False
                break
    if not source_valid:
        blockers.append(_completion_blocker(
            "RUN_RECEIPT_INCOMPLETE",
            "Run task/source identity is incomplete or invalid.",
        ))

    runtime = receipt.get("runtime")
    runtime_valid = isinstance(runtime, dict)
    session_id = runtime.get("sessionId") if runtime_valid else None
    pipe = runtime.get("pipe") if runtime_valid else None
    if runtime_valid:
        required_strings = (
            "manifestPath", "sessionId", "tag", "exe", "exeSha256",
            "pipe", "appDataRoot", "cacheRoot",
        )
        runtime_valid = (
            runtime.get("source") == "lanista-session-manifest"
            and all(isinstance(runtime.get(key), str) and bool(runtime[key].strip()) for key in required_strings)
            and isinstance(runtime.get("pid"), int)
            and not isinstance(runtime.get("pid"), bool)
            and runtime["pid"] > 0
            and re.fullmatch(r"[0-9a-f]{64}", runtime["exeSha256"]) is not None
            and pipe == f"ColosseumLanista-{session_id}"
        )
    if not runtime_valid:
        blockers.append(_completion_blocker(
            "RUN_RUNTIME_NOT_BOUND",
            "Run has no complete bound Lanista runtime.",
        ))

    desktop_evidence = receipt.get("desktopEvidence")
    if not isinstance(desktop_evidence, list) or not desktop_evidence:
        blockers.append(_completion_blocker(
            "RUN_DESKTOP_EVIDENCE_MISSING",
            "Run has no desktop evidence.",
        ))
    else:
        for raw_path in desktop_evidence:
            if not isinstance(raw_path, str) or not raw_path.strip():
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_EVIDENCE_INVALID",
                    "Run contains an invalid desktop evidence path.",
                ))
                continue
            evidence_path = Path(raw_path)
            if not evidence_path.is_file():
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_EVIDENCE_INVALID",
                    "Desktop evidence file is missing.",
                    path=raw_path,
                ))
                continue
            if evidence_path.suffix.casefold() == ".png":
                continue
            if evidence_path.suffix.casefold() != ".json":
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_EVIDENCE_INVALID",
                    "Desktop evidence must be a screenshot PNG or action JSON.",
                    path=raw_path,
                ))
                continue
            try:
                action = json.loads(evidence_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                action = None
            if not isinstance(action, dict):
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_EVIDENCE_INVALID",
                    "Desktop action evidence cannot be read.",
                    path=raw_path,
                ))
                continue
            state = action.get("verificationState")
            if state == "PENDING_VISUAL_REVIEW":
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_ACTION_PENDING",
                    "Desktop action still awaits visual review.",
                    path=raw_path,
                ))
            elif state == "VISUAL_OUTCOME_UNCERTAIN":
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_ACTION_UNCERTAIN",
                    "Desktop action outcome is uncertain.",
                    path=raw_path,
                ))
            elif state == "VISUALLY_REJECTED":
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_ACTION_REJECTED",
                    "Desktop action was visually rejected.",
                    path=raw_path,
                ))
            elif state != "VISUALLY_CONFIRMED" or action.get("visualVerdict") != "pass":
                blockers.append(_completion_blocker(
                    "RUN_DESKTOP_EVIDENCE_INVALID",
                    "Desktop action evidence is not visually confirmed.",
                    path=raw_path,
                ))
            else:
                if action.get("toolError") is not None:
                    blockers.append(_completion_blocker(
                        "RUN_DESKTOP_ACTION_FAILED",
                        "Desktop action recorded a tool failure.",
                        path=raw_path,
                    ))
                for capture_name in ("before", "after"):
                    capture = action.get(capture_name)
                    capture_valid = isinstance(capture, dict)
                    screenshot_path = capture.get("screenshotPath") if capture_valid else None
                    screenshot_sha = capture.get("screenshotSha256") if capture_valid else None
                    screenshot_bytes = capture.get("screenshotBytes") if capture_valid else None
                    if (
                        not isinstance(screenshot_path, str)
                        or not screenshot_path.strip()
                        or not isinstance(screenshot_sha, str)
                        or re.fullmatch(r"[0-9a-fA-F]{64}", screenshot_sha) is None
                        or isinstance(screenshot_bytes, bool)
                        or not isinstance(screenshot_bytes, int)
                        or screenshot_bytes < 0
                    ):
                        capture_valid = False
                    raw_capture = b""
                    if capture_valid:
                        try:
                            raw_capture = Path(screenshot_path).read_bytes()
                        except OSError:
                            capture_valid = False
                    if capture_valid:
                        capture_valid = (
                            len(raw_capture) == screenshot_bytes
                            and hashlib.sha256(raw_capture).hexdigest().casefold() == screenshot_sha.casefold()
                        )
                    if not capture_valid:
                        blockers.append(_completion_blocker(
                            "RUN_DESKTOP_EVIDENCE_INVALID",
                            f"Desktop action {capture_name} screenshot evidence is missing or changed.",
                            path=raw_path,
                        ))
                        break

    verification = receipt.get("verification")
    frozen_checks = verification.get("selectedChecks") if isinstance(verification, dict) else None
    result = receipt.get("result")
    verification_result = result.get("verification") if isinstance(result, dict) else None
    verification_complete = (
        isinstance(frozen_checks, list)
        and bool(frozen_checks)
        and isinstance(verification_result, dict)
    )
    if verification_complete:
        recorded_checks = verification_result.get("checks")
        recorded_selected = verification_result.get("selectedChecks")
        verification_complete = (
            verification_result.get("ok") is True
            and recorded_selected == frozen_checks
            and isinstance(recorded_checks, list)
            and len(recorded_checks) == len(frozen_checks)
            and all(
                isinstance(item, dict) and item.get("exitCode") == 0
                for item in recorded_checks
            )
        )
    if not verification_complete:
        code = (
            "RUN_VERIFICATION_FAILED"
            if isinstance(verification_result, dict) and verification_result.get("ok") is False
            else "RUN_VERIFICATION_INCOMPLETE"
        )
        blockers.append(_completion_blocker(
            code,
            "Frozen verification checks have not all passed.",
        ))

    frozen_journeys = verification.get("selectedJourneys") if isinstance(verification, dict) else None
    journey_result = result.get("journey") if isinstance(result, dict) else None
    journey_complete = (
        isinstance(frozen_journeys, list)
        and len(frozen_journeys) == 1
        and isinstance(frozen_journeys[0], dict)
        and isinstance(journey_result, dict)
    )
    if journey_complete:
        frozen_journey = frozen_journeys[0]
        execution = journey_result.get("execution")
        journey_complete = (
            journey_result.get("ok") is True
            and journey_result.get("selector") == frozen_journey.get("selector")
            and journey_result.get("path") == frozen_journey.get("path")
            and isinstance(execution, dict)
            and execution.get("exitCode") == 0
            and runtime_valid
            and journey_result.get("sessionId") == session_id
            and journey_result.get("pipe") == pipe
        )
    if not journey_complete:
        code = (
            "RUN_JOURNEY_FAILED"
            if isinstance(journey_result, dict) and journey_result.get("ok") is False
            else "RUN_JOURNEY_INCOMPLETE"
        )
        blockers.append(_completion_blocker(
            code,
            "Frozen Lanista journey has not passed against the bound runtime.",
        ))

    return blockers

def apply_run_completion_gate(receipt: dict[str, Any]) -> list[dict[str, Any]]:
    blockers = run_completion_blockers(receipt)
    receipt["completionReady"] = not blockers
    receipt["completionBlockers"] = blockers
    return blockers

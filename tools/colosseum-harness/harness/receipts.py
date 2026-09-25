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

from .repo import HarnessError, envelope, normalize_repo_paths, repo_snapshot
from .evidence import apply_run_completion_gate, load_lanista_session_manifest

def _run_source_identity(root: Path, relative_path: str) -> dict[str, Any]:
    root = root.resolve()
    candidate = (root / relative_path).resolve()
    if candidate != root and root not in candidate.parents:
        raise HarnessError(
            "RUN_SCOPE_OUTSIDE_REPO",
            f"Run path resolves outside the repository: {relative_path}",
        )
    if candidate.exists() and candidate.is_dir():
        raise HarnessError(
            "RUN_SCOPE_NOT_FILE",
            f"Run paths must name files, not directories: {relative_path}",
        )
    if not candidate.exists():
        return {
            "path": relative_path,
            "exists": False,
            "sha256": None,
            "sizeBytes": None,
        }
    if not candidate.is_file():
        raise HarnessError(
            "RUN_SCOPE_NOT_FILE",
            f"Run path is not a regular file: {relative_path}",
        )
    raw = candidate.read_bytes()
    return {
        "path": relative_path,
        "exists": True,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "sizeBytes": len(raw),
    }

def create_run_receipt(
    root: Path,
    task: str,
    paths: list[str],
    map_path: str,
    selected_checks: list[dict[str, Any]],
    warnings: list[str],
    selected_journeys: list[dict[str, Any]] | None = None,
) -> tuple[dict[str, Any], Path]:
    normalized_paths = normalize_repo_paths(paths, "run path")
    if not normalized_paths:
        raise HarnessError(
            "RUN_SCOPE_REQUIRED",
            "--record-run requires at least one explicit --path.",
        )
    resolved_map = Path(map_path).resolve()
    if not resolved_map.is_file():
        raise HarnessError("MAP_NOT_FOUND", f"Map not found: {resolved_map}")

    snapshot = repo_snapshot(root)
    run_id = f"run_{uuid.uuid4().hex}"
    receipt = {
        "schema": "colosseum.harness.run.v1",
        "runId": run_id,
        "task": task.strip(),
        "repo": {
            "root": snapshot["root"],
            "head": snapshot["head"],
            "branch": snapshot["branch"],
        },
        "paths": normalized_paths,
        "source": [_run_source_identity(root, path) for path in normalized_paths],
        "map": {
            "path": str(resolved_map),
            "sha256": hashlib.sha256(resolved_map.read_bytes()).hexdigest(),
        },
        "verification": {
            "selectedChecks": selected_checks,
            "selectedJourneys": list(selected_journeys or []),
            "warnings": list(warnings),
        },
        "build": None,
        "runtime": None,
        "desktopEvidence": [],
        "result": None,
        "completionReady": False,
    }
    apply_run_completion_gate(receipt)
    receipt_path = root / "artifacts" / "harness-runs" / run_id / "run.json"
    receipt_path.parent.mkdir(parents=True, exist_ok=False)
    with receipt_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(receipt, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    return receipt, receipt_path

def load_run_receipt(path: Path | str) -> dict[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError("RUN_RECEIPT_INVALID", f"Cannot read run receipt: {path}") from exc
    if not isinstance(value, dict) or value.get("schema") != "colosseum.harness.run.v1":
        raise HarnessError("RUN_RECEIPT_INVALID", f"Invalid run receipt: {path}")
    return value

def _run_receipt_path(root: Path, run_id: str) -> Path:
    if not re.fullmatch(r"run_[0-9a-f]{32}", run_id):
        raise HarnessError("RUN_ID_INVALID", f"Invalid run id: {run_id}")
    return root / "artifacts" / "harness-runs" / run_id / "run.json"

def _write_run_receipt(path: Path, receipt: dict[str, Any]) -> None:
    temp = path.with_name(f".run.{uuid.uuid4().hex}.tmp")
    try:
        with temp.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(receipt, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temp, path)
    finally:
        try:
            temp.unlink()
        except FileNotFoundError:
            pass

def bind_run_session(root: Path, run_id: str, session_path: Path | str) -> tuple[dict[str, Any], Path]:
    receipt_path = _run_receipt_path(root, run_id)
    receipt = load_run_receipt(receipt_path)
    if receipt.get("runId") != run_id or receipt.get("repo", {}).get("root") != str(root.resolve()):
        raise HarnessError("RUN_RECEIPT_INVALID", f"Run receipt identity mismatch: {receipt_path}")
    if receipt.get("runtime") is not None:
        raise HarnessError(
            "RUN_RUNTIME_ALREADY_BOUND",
            f"Run already has runtime identity: {run_id}",
        )

    manifest, resolved_session_path = load_lanista_session_manifest(session_path)
    receipt["runtime"] = {
        "source": "lanista-session-manifest",
        "manifestPath": str(resolved_session_path),
        "sessionId": manifest["sessionId"],
        "tag": manifest["tag"],
        "exe": manifest["exe"],
        "exeSha256": manifest["exeSha256"].lower(),
        "pid": int(manifest["pid"]),
        "pipe": manifest["pipe"],
        "appDataRoot": manifest["appDataRoot"],
        "cacheRoot": manifest["cacheRoot"],
    }
    apply_run_completion_gate(receipt)
    _write_run_receipt(receipt_path, receipt)
    return receipt, receipt_path

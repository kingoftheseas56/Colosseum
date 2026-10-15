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

DRIVE_COMMANDS = {"ui-click", "ui-keypress", "ui-text-input", "ui-scroll", "window-set-state", "reader2-hot-switch"}

def load_scenario(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError("SCENARIO_INVALID", f"Cannot read scenario: {path}", str(exc)) from exc
    if not isinstance(doc, dict) or not isinstance(doc.get("steps"), list):
        raise HarnessError("SCENARIO_INVALID", f"Scenario lacks a steps array: {path}")
    return doc

def discover_journeys(root: Path) -> list[dict[str, Any]]:
    scenario_dir = root / "tests" / "lanista_scenarios"
    if not scenario_dir.is_dir():
        return []

    out = []
    for path in sorted(scenario_dir.glob("*.json")):
        rel = path.relative_to(root).as_posix()
        try:
            doc = load_scenario(path)
        except HarnessError as exc:
            out.append({
                "name": path.stem,
                "file": path.name,
                "path": rel,
                "valid": False,
                "usesDriveCommands": None,
                "comment": "",
                "error": {"code": exc.code, "message": exc.message, "details": exc.details},
            })
            continue
        commands = {
            step.get("cmd") for step in doc["steps"]
            if isinstance(step, dict) and isinstance(step.get("cmd"), str)
        }
        out.append({
            "name": doc.get("name") or path.stem,
            "file": path.name,
            "path": rel,
            "valid": True,
            "usesDriveCommands": bool(commands & DRIVE_COMMANDS),
            "comment": doc.get("comment", ""),
        })
    return out

def resolve_journey(root: Path, selector: str) -> dict[str, Any]:
    query = selector.strip().lower().replace("\\", "/")
    matches = [
        item for item in discover_journeys(root)
        if query in {
            item["name"].lower(),
            item["file"].lower(),
            item["path"].lower(),
            Path(item["file"]).stem.lower(),
        }
    ]
    if not matches:
        raise HarnessError("JOURNEY_NOT_FOUND", f"No exact Lanista scenario match for: {selector}")
    if len(matches) != 1:
        raise HarnessError("JOURNEY_AMBIGUOUS", f"Multiple scenarios match: {selector}", matches)
    match = matches[0]
    if not match.get("valid", True):
        error = match.get("error", {})
        raise HarnessError(
            error.get("code", "SCENARIO_INVALID"),
            error.get("message", f"Scenario is invalid: {match['path']}"),
            error.get("details"),
        )
    return match

def journey_argv(
    root: Path, journey: dict[str, Any], *, mode: str,
    drive: bool, seed: str | None, ready_ms: int | None,
    pipe: str | None,
) -> list[str]:
    exe = root / "native" / "build-msvc" / ("lanista.exe" if os.name == "nt" else "lanista")
    scenario = journey["path"]
    if mode == "session":
        argv = [str(exe), "session", "run", scenario]
        if drive:
            argv.append("--drive")
        if seed:
            seed_path = Path(seed)
            if not seed_path.is_absolute():
                seed_path = root / seed_path
            argv.extend(["--seed", str(seed_path)])
        if ready_ms is not None:
            argv.extend(["--ready-ms", str(ready_ms)])
        return argv
    argv = [str(exe)]
    if pipe:
        argv.extend(["--pipe", pipe])
    argv.extend(["run", scenario])
    return argv

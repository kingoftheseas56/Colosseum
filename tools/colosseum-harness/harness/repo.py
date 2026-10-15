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

class HarnessError(RuntimeError):
    def __init__(self, code: str, message: str, details: Any = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details

def norm(value: str | Path) -> str:
    return str(value).replace("\\", "/").strip("/").lower()

def looks_like_colosseum(path: Path) -> bool:
    return all((path / part).exists() for part in ("native", "qml", "tests"))

def resolve_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env_root = os.environ.get("COLOSSEUM_ROOT")
    if env_root:
        candidates.append(Path(env_root))
    repo_from_harness = Path(__file__).resolve().parents[3]
    candidates.extend([
        Path.cwd(),
        repo_from_harness,
    ])
    for candidate in candidates:
        try:
            resolved = candidate.expanduser().resolve()
        except OSError:
            continue
        if looks_like_colosseum(resolved):
            return resolved
    raise HarnessError(
        "COLOSSEUM_ROOT_NOT_FOUND",
        "Pass --root or set COLOSSEUM_ROOT.",
        [str(p) for p in candidates],
    )

def run(argv: list[str], cwd: Path, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            argv, cwd=str(cwd), capture_output=True,
            text=True, timeout=timeout, check=False,
        )
    except FileNotFoundError as exc:
        raise HarnessError("RUNNER_MISSING", f"Runner not found: {argv[0]}", argv) from exc
    except subprocess.TimeoutExpired as exc:
        raise HarnessError("COMMAND_TIMEOUT", f"Timed out after {timeout}s", argv) from exc

def git(root: Path, *args: str) -> str:
    proc = run(["git", "-C", str(root), *args], root, 30)
    if proc.returncode:
        raise HarnessError(
            "GIT_ERROR", f"git {' '.join(args)} failed",
            {"exitCode": proc.returncode, "stderr": proc.stderr.strip()},
        )
    return proc.stdout.rstrip("\r\n")

def repo_snapshot(root: Path) -> dict[str, Any]:
    return {
        "root": str(Path(git(root, "rev-parse", "--show-toplevel")).resolve()),
        "head": git(root, "rev-parse", "HEAD"),
        "branch": git(root, "branch", "--show-current") or "(detached)",
        "dirty": [x for x in git(root, "status", "--porcelain=v1", "-z").split("\0") if x],
    }

def envelope(
    command: str, root: Path, *, ok: bool = True,
    data: Any = None, evidence: list[Any] | None = None,
    warnings: list[str] | None = None, error: dict[str, Any] | None = None,
) -> dict[str, Any]:
    out = {
        "ok": ok,
        "command": command,
        "repo": repo_snapshot(root),
        "data": {} if data is None else data,
        "evidence": evidence or [],
        "warnings": warnings or [],
    }
    if error is not None:
        out["error"] = error
    return out

def dirty_paths(snapshot: dict[str, Any]) -> list[str]:
    out = []
    for record in snapshot.get("dirty", []):
        text = record[3:] if len(record) >= 4 else record
        if " -> " in text:
            text = text.split(" -> ", 1)[1]
        out.append(text.replace("\\", "/"))
    return out

def normalize_repo_paths(values: list[str], label: str = "path") -> list[str]:
    out: list[str] = []
    for value in values:
        if not isinstance(value, str) or not value.strip():
            raise HarnessError("INVALID_REPO_PATH", f"{label} must be a non-empty repo-relative path.")
        candidate = Path(value)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise HarnessError("INVALID_REPO_PATH", f"{label} must stay inside the repository: {value}")
        normalized = value.replace("\\", "/").strip("/")
        if normalized and normalized not in out:
            out.append(normalized)
    return out

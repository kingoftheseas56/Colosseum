#!/usr/bin/env python3
"""Fail if a staged package still contains the retired external stream runtime."""

from __future__ import annotations

import sys
from pathlib import Path


FORBIDDEN_NAMES = {"server.js", "stremio-runtime.exe"}
FORBIDDEN_DIRECTORY = "stream_server"
REQUIRED_MEDIA_TOOLS = (
    Path("native") / "build-msvc" / "tools" / "ffmpeg.exe",
    Path("native") / "build-msvc" / "tools" / "ffprobe.exe",
)


def find_forbidden(root: Path) -> list[Path]:
    violations: list[Path] = []
    for path in root.rglob("*"):
        if path.name.lower() in FORBIDDEN_NAMES:
            violations.append(path)
            continue
        if any(part.lower() == FORBIDDEN_DIRECTORY for part in path.parts):
            violations.append(path)
    return sorted(set(violations))


def find_missing_required(root: Path) -> list[Path]:
    return [root / relative for relative in REQUIRED_MEDIA_TOOLS
            if not (root / relative).is_file()]


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} STAGE", file=sys.stderr)
        return 2
    root = Path(argv[1]).resolve()
    if not root.is_dir():
        print(f"native package stage does not exist: {root}", file=sys.stderr)
        return 2
    violations = find_forbidden(root)
    if violations:
        print("retired external stream runtime found in native package:", file=sys.stderr)
        for path in violations:
            print(f"  {path}", file=sys.stderr)
        return 1
    missing = find_missing_required(root)
    if missing:
        print("native package is missing required media tool(s):", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 1
    print(f"NATIVE_PACKAGE_CONTENT_OK root={root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

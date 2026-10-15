#!/usr/bin/env python3
"""Reject maintainer-private path tokens from Git-tracked public files."""

from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path


PRIVATE_USER_HASHES = {
    "25651f75eddf282c484cabb553083d947f9d3cf566b7951b9b72192e7ec8b25c",
}
USER_ROOT_RE = re.compile(
    rb"(?:[a-z]:[\\/]+users[\\/]+|file:[\\/]{2,}[a-z]:[\\/]+users[\\/]+|"
    rb"/mnt/[a-z]/users/|/users/|/home/)(?P<user>[^\\/\s\"'<>]+)",
    re.IGNORECASE,
)
TOKEN_RE = re.compile(rb"(?<![A-Za-z0-9_.-])([A-Za-z][A-Za-z0-9_.-]{2,63})(?![A-Za-z0-9_.-])")


def private_token(value: bytes) -> bool:
    digest = hashlib.sha256(value.lower()).hexdigest()
    return digest in PRIVATE_USER_HASHES


def git_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("public path guard must run inside a Git worktree")
    return Path(result.stdout.strip()).resolve()


def tracked_paths(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("git ls-files failed")
    names = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
    return [root / name for name in names if name]


def line_rules(line: bytes) -> set[str]:
    rules: set[str] = set()
    for match in USER_ROOT_RE.finditer(line):
        if private_token(match.group("user")):
            rules.add("private absolute user path")
    if any(private_token(match.group(1)) for match in TOKEN_RE.finditer(line)):
        rules.add("private maintainer username token")
    return rules


def scan(root: Path) -> list[tuple[str, int, str]]:
    findings: list[tuple[str, int, str]] = []
    for path in tracked_paths(root):
        if path.is_symlink():
            data = path.readlink().as_posix().encode("utf-8")
        elif path.is_file():
            data = path.read_bytes()
        else:
            continue
        relative = path.relative_to(root).as_posix()
        for line_number, line in enumerate(data.splitlines(), start=1):
            for rule in sorted(line_rules(line)):
                findings.append((relative, line_number, rule))
    return findings


def main() -> int:
    try:
        root = git_root()
        findings = scan(root)
    except (OSError, RuntimeError) as exc:
        print(f"public-path guard error: {exc}", file=sys.stderr)
        return 2

    if not findings:
        print("PUBLIC_PATH_GUARD_OK")
        return 0

    print(f"PUBLIC_PATH_GUARD_FAILED: {len(findings)} finding(s)")
    for relative, line_number, rule in findings:
        print(f"{relative}:{line_number}: {rule}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

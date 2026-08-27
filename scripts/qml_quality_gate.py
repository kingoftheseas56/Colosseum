#!/usr/bin/env python3
"""Run qmllint with hard bug categories and an explicit known-debt baseline."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

HARD_CATEGORIES = (
    "alias-cycle",
    "assignment-in-condition",
    "duplicate-property-binding",
    "signal-handler-parameters",
    "unreachable-code",
    "var-used-before-declaration",
    "with",
)
FINDING_RE = re.compile(
    r"^(?:Warning|Error):\s+(.+?\.qml):(\d+):(\d+):\s+(.*?)\s+\[([^\]]+)\]\s*$"
)


def chunks(items: list[Path], size: int):
    for start in range(0, len(items), size):
        yield items[start:start + size]


def load_baseline(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")}

def finding_fingerprints(output: str, root: Path) -> set[str]:
    findings: set[str] = set()
    root_parent = root.resolve().parent
    for line in output.splitlines():
        match = FINDING_RE.match(line.strip())
        if not match:
            continue
        raw_path, _, _, message, category = match.groups()
        if category not in HARD_CATEGORIES or message.startswith("Note:"):
            continue
        path = Path(raw_path)
        try:
            relative = path.resolve().relative_to(root_parent).as_posix()
        except ValueError:
            normalized = path.as_posix()
            marker = "/qml/"
            relative = "qml/" + normalized.split(marker, 1)[-1] if marker in normalized else normalized
        findings.add(f"{relative}|{category}|{message}")
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmllint", required=True)
    parser.add_argument("--root", default="qml")
    parser.add_argument("--baseline", default=".github/qml-quality-baseline.txt")
    parser.add_argument("--batch-size", type=int, default=24)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    files = sorted(root.rglob("*.qml"))
    if not files:
        print("QML_QUALITY_FAIL=no_qml_files", file=sys.stderr)
        return 2

    base = [args.qmllint, "--max-warnings", "-1", "-I", str(root)]
    for category in HARD_CATEGORIES:
        base.extend((f"--{category}", "warning"))

    findings: set[str] = set()
    runtime_failures: list[str] = []
    for batch in chunks(files, max(1, args.batch_size)):
        completed = subprocess.run(
            base + [str(path) for path in batch],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        findings.update(finding_fingerprints(completed.stdout, root))
        if completed.returncode != 0:
            errors = [line for line in completed.stdout.splitlines() if line.startswith("Error:")]
            runtime_failures.extend(errors or completed.stdout.splitlines()[-40:])

    baseline = load_baseline(Path(args.baseline))
    new_findings = findings - baseline
    stale_baseline = baseline - findings
    if runtime_failures or new_findings:
        print(f"QML_QUALITY_FAIL files={len(files)} new_findings={len(new_findings)} "
              f"runtime_failures={len(runtime_failures)}", file=sys.stderr)
        for finding in sorted(new_findings):
            print(f"NEW_QML_FINDING={finding}", file=sys.stderr)
        for line in runtime_failures:
            print(line, file=sys.stderr)
        return 1

    print(f"QML_QUALITY_OK files={len(files)} known_findings={len(findings & baseline)}")
    if stale_baseline:
        for finding in sorted(stale_baseline):
            print(f"STALE_QML_BASELINE={finding}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

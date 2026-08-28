#!/usr/bin/env python3
"""Run Colosseum's high-signal clang-tidy policy on shipped first-party C++."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

CHECKS = (
    "clang-analyzer-core.*",
    "clang-analyzer-cplusplus.NewDelete*",
    "clang-analyzer-deadcode.DeadStores",
    "bugprone-chained-comparison",
    "bugprone-empty-catch",
    "bugprone-multi-level-implicit-pointer-conversion",
    "bugprone-suspicious-include",
    "cert-dcl59-cpp",
    "cert-err33-c",
)
DIAGNOSTIC_RE = re.compile(
    r"^(.*?):(\d+):(\d+):\s+(warning|error):\s+(.*?)\s+\[([^\]]+)\]\s*$"
)


@dataclass(frozen=True, order=True)
class Diagnostic:
    path: str
    line: int
    check: str
    message: str

    @property
    def fingerprint(self) -> str:
        return f"{self.path}|{self.line}|{self.check}|{self.message}"


def _normalized_output(entry: dict[str, str]) -> str:
    return str(entry.get("output", "")).replace("\\", "/").lower()


def select_application_entries(
    entries: list[dict[str, str]], source_root: Path
) -> list[dict[str, str]]:
    source_root = source_root.resolve()
    selected: dict[str, dict[str, str]] = {}
    for entry in entries:
        raw_file = entry.get("file")
        if not raw_file or "cmakefiles/colosseum.dir/" not in _normalized_output(entry):
            continue
        path = Path(raw_file).resolve()
        try:
            relative = path.relative_to(source_root)
        except ValueError:
            continue
        parts = {part.lower() for part in relative.parts}
        if path.suffix.lower() not in {".cpp", ".cc", ".cxx"}:
            continue
        if "third_party" in parts or "prototypes" in parts:
            continue
        if any(part.lower().startswith("build-") for part in relative.parts):
            continue
        if any("_autogen" in part.lower() for part in relative.parts):
            continue
        selected[str(path).lower()] = entry
    return [selected[key] for key in sorted(selected)]


def _relative_first_party(raw_path: str, source_root: Path) -> str | None:
    path = Path(raw_path).resolve()
    source_root = source_root.resolve()
    try:
        relative = path.relative_to(source_root.parent)
    except ValueError:
        return None
    if not relative.parts or relative.parts[0].lower() != source_root.name.lower():
        return None
    parts = {part.lower() for part in relative.parts}
    if "third_party" in parts or "prototypes" in parts:
        return None
    if any(part.lower().startswith("build-") for part in relative.parts):
        return None
    if any("_autogen" in part.lower() for part in relative.parts):
        return None
    return relative.as_posix()


def parse_diagnostics(output: str, source_root: Path) -> set[Diagnostic]:
    findings: set[Diagnostic] = set()
    for line in output.splitlines():
        match = DIAGNOSTIC_RE.match(line.strip())
        if not match:
            continue
        raw_path, line, _, _, message, check = match.groups()
        relative = _relative_first_party(raw_path, source_root)
        if relative is None:
            continue
        findings.add(Diagnostic(relative, int(line), check, message))
    return findings


def load_allowlist(path: Path) -> set[Diagnostic]:
    if not path.is_file():
        return set()
    findings: set[Diagnostic] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|", 3)
        if len(parts) != 4:
            raise ValueError(f"invalid clang-tidy allowlist entry: {line}")
        findings.add(Diagnostic(parts[0], int(parts[1]), parts[2], parts[3]))
    return findings


def _clang_tidy_version(executable: str) -> str:
    completed = subprocess.run(
        [executable, "--version"], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout.strip() or "clang-tidy --version failed")
    return completed.stdout


def _run_one(
    executable: str, source: str, database: Path, checks: str
) -> tuple[str, int, str]:
    completed = subprocess.run(
        [
            executable,
            source,
            f"-p={database}",
            f"-checks={checks}",
            "--quiet",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        errors="replace",
    )
    return source, completed.returncode, completed.stdout


def _write_filtered_database(entries: list[dict[str, str]], path: Path) -> None:
    path.write_text(json.dumps(entries, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--source-root", default="native")
    parser.add_argument("--allowlist", default=".github/clang-tidy-allowlist.txt")
    parser.add_argument("--required-version", default="22.1.8")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--min-files", type=int, default=1)
    args = parser.parse_args()

    version = _clang_tidy_version(args.clang_tidy)
    if args.required_version not in version:
        print(
            f"CLANG_TIDY_FAIL=version expected={args.required_version!r} actual={version.strip()!r}",
            file=sys.stderr,
        )
        return 2

    compile_commands = Path(args.compile_commands).resolve()
    source_root = Path(args.source_root).resolve()
    entries = json.loads(compile_commands.read_text(encoding="utf-8-sig"))
    selected = select_application_entries(entries, source_root)
    if len(selected) < args.min_files:
        print(
            f"CLANG_TIDY_FAIL=coverage files={len(selected)} min_files={args.min_files}",
            file=sys.stderr,
        )
        return 2

    checks = ",".join(("-*",) + CHECKS)
    findings: set[Diagnostic] = set()
    runtime_failures: list[tuple[str, int, str]] = []
    with tempfile.TemporaryDirectory(prefix="colosseum-clang-tidy-") as tmp:
        database = Path(tmp)
        _write_filtered_database(selected, database / "compile_commands.json")
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
            futures = [
                executor.submit(_run_one, args.clang_tidy, entry["file"], database, checks)
                for entry in selected
            ]
            for future in as_completed(futures):
                source, returncode, output = future.result()
                findings.update(parse_diagnostics(output, source_root))
                if returncode != 0:
                    runtime_failures.append((source, returncode, output))

    allowlist = load_allowlist(Path(args.allowlist))
    new_findings = findings - allowlist
    accepted = findings & allowlist
    stale = allowlist - findings

    if runtime_failures or new_findings:
        print(
            f"CLANG_TIDY_FAIL files={len(selected)} new_findings={len(new_findings)} "
            f"runtime_failures={len(runtime_failures)}",
            file=sys.stderr,
        )
        for finding in sorted(new_findings):
            print(f"NEW_CLANG_TIDY_FINDING={finding.fingerprint}", file=sys.stderr)
        for source, returncode, output in runtime_failures:
            print(f"CLANG_TIDY_RUNTIME_FAIL={source}|exit={returncode}", file=sys.stderr)
            for line in output.splitlines()[-40:]:
                print(line, file=sys.stderr)
        return 1

    print(
        f"CLANG_TIDY_OK files={len(selected)} actionable_findings=0 "
        f"accepted_false_positives={len(accepted)}"
    )
    for finding in sorted(stale):
        print(f"STALE_CLANG_TIDY_ALLOWLIST={finding.fingerprint}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

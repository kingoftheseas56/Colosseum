#!/usr/bin/env python3
"""
Colosseum Guardian Loop - the incident packet builder (Slice G3).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G3 ("The incident
packet - from a failed run directory to an agent-grade bug report"). Turns "Vault broken"
into "on commit X, step 37 failed: expected A, got B - here is the tree, the logs, the
state, and the exact command that reproduces it."

    python scripts/autorepair/incident.py --from-run <artifacts/lanista-sessions/<id>>

produces artifacts/autorepair/AR-<YYYY-MM-DD>-<NNNN>/ holding the D8 file set:

    incident.json     schema v1 - id, baseSha, scenario, seed, tag, failing step index +
                       label, expected vs got, exit code, and the A7 dedup fingerprint.
    failure.log        copied verbatim from the run dir (see "Run-dir contract" below).
    stdout.log          "
    stderr.log          "
    colosseum.log       "  (best-effort - simply absent if the run dir never carried one)
    journey.json        the scenario file, read from the CURRENT tree at the recorded
                         scenario path (D1: this builder is decoupled from Night Watch and
                         consumes ANY failed run directory, from the tree as it stands
                         today - no frozen-at-run-time copy exists anywhere to pull from).
    grabs/*.png         every *.png found directly under the run dir (lanista's own
                         "session run" pulls the app-side grabs into the run dir before the
                         tagged tree is considered disposable - native/tools/lanista.cpp,
                         the "Pull the app-side artifacts" block).
    screen.png          the failing step's own evidence grab, if the run recorded one -
                         D8's canonical single-exhibit name, alongside the fuller grabs/.
    warnings.json       tests/warning_gate.ps1's verdict over colosseum.log + stderr.log.
    vault-forensics.json   ONLY when the scenario touches a Vault surface (heuristic
                            below); correctly absent for a manga/comic/video/etc. journey.
    ui-tree.json        honest, explicit absence for a post-mortem packet - see below,
                         never fabricated.
    environment.json    HEAD sha, dirty-file list, best-effort toolchain versions.
    reproduce.ps1       the exact `lanista session run ...` line that reproduces the
                         failure (Fixture fragility law: embed exact commands, never rely
                         on ambient state).

Run-dir contract (this slice DEFINES it - D1 decouples the builder from Night Watch, so no
upstream producer has written this contract before now):

  session.json   REQUIRED. Written by lanista.exe itself (native/tools/lanista.cpp,
                 Session::writeManifest) - exe, qml, tag, seedDir, appDataRoot, cacheRoot,
                 pid, drive, launchedAt/readyAt/exitedAt, exitCode (the APP PROCESS's exit
                 code, almost always 0 - a failed STEP is not a crashed process), crashed,
                 killReason. Missing, unreadable, or not a JSON object -> MalformedRunDirError.

  failure.log    REQUIRED. lanista.exe's `session run` verb never writes its own step
                 trace to disk anywhere - only to its own stdout (verified empirically this
                 slice by reading native/tools/lanista.cpp end to end: the "session" branch
                 calls s.writeManifest() and nothing else touches a report file). So the
                 run-dir PRODUCER (this slice's own minting step today; a future Night
                 Watch wrapper later) must capture that stdout itself - exactly the pattern
                 the house's own tests/test_update_lanista.ps1 already uses with
                 `Tee-Object`. This builder additionally requires the FIRST non-empty line
                 to be the literal invocation, `$ <the full command>`: lanista's own
                 manifest carries no scenario-path field at all, so this is the only place
                 the scenario path (and a belt-and-braces copy of seed/tag/exe/qml/
                 ready-ms/flags) can be recovered from. Missing entirely, empty, or an
                 unparsable first line -> MalformedRunDirError.

  stdout.log/stderr.log   written directly by lanista.exe (Session::startSession's
                          setStandardOutputFile/setStandardErrorFile) - the APP's own
                          process streams. Always present once a session ever started;
                          copied as-is when present.

  colosseum.log  best-effort. AppLog's own leveled file lives under the (disposable,
                 tagged) AppData root, not the run dir - this builder never reaches into
                 AppData itself (post-mortem: that root may already be gone by the time an
                 incident is built). The minting step is expected to have copied it in
                 alongside failure.log; if genuinely absent, warnings.json is still built
                 from stderr.log alone and colosseum.log is simply not copied (never
                 fabricated).

  *.png          optional. Grabs lanista already copied into the run dir itself.

Read-only contract: a run directory is NEVER mutated by this module (D8: "Behavior to
preserve: run directories are read-only inputs").

A7 (binding pressure-test amendment) - incident dedup: incident.json carries `fingerprint`,
a stable hash of (scenario path, failing step label, expected, got). Opening a new incident
whose fingerprint matches an existing, still-OPEN incident under the artifacts root is
refused (DuplicateIncidentError) with a pointer to the existing one - "no five-copies-of-
one-flake mornings." Nothing in the Guardian Loop closes an incident yet (that lands with a
later slice); until it does, every incident under the artifacts root is open by definition.
The dedup scan honours a `closedAt` key in incident.json or a `CLOSED` marker file in the
incident dir as a forward-compatible "this one's done" escape hatch for that later slice -
unused by any slice today, costs nothing to check for now.

Stdlib only (house pattern - scripts/autorepair/policy.py, scripts/soak-digest.py). Shells
out only to `git` (best-effort, never fatal - see _git()) and `pwsh`/`powershell` for
tests/warning_gate.ps1 (also never fatal - see _run_warning_gate()).

Public API (parameterized like policy.py's load_policy()/sandbox.py's create(), so tests
can point every root at a temp dir and pin `now` for full determinism):

    build_incident(run_dir, *, artifacts_root=DEFAULT_ARTIFACTS_ROOT,
                    repo_root=REPO_ROOT, now=None) -> IncidentResult
    compute_fingerprint(scenario, failing_label, expected, got) -> str
    parse_failure_log(text) -> ParsedFailureLog

Usage:
    python scripts/autorepair/incident.py --from-run artifacts/lanista-sessions/<id>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

# scripts/autorepair/incident.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARTIFACTS_ROOT = REPO_ROOT / "artifacts" / "autorepair"

SCHEMA = 1

__all__ = [
    "REPO_ROOT",
    "DEFAULT_ARTIFACTS_ROOT",
    "SCHEMA",
    "IncidentError",
    "MalformedRunDirError",
    "NoFailureError",
    "DuplicateIncidentError",
    "StepResult",
    "ParsedFailureLog",
    "IncidentResult",
    "parse_failure_log",
    "compute_fingerprint",
    "build_incident",
]


# ── errors: every refusal is clean and named, never a bare traceback ───────


class IncidentError(RuntimeError):
    """Base for every clean, named incident-builder refusal."""


class MalformedRunDirError(IncidentError):
    """The run dir is missing, or fails to satisfy the run-dir contract documented above."""


class NoFailureError(IncidentError):
    """The run dir describes a run where every step passed - "no failure to report"."""


class DuplicateIncidentError(IncidentError):
    """A7: an OPEN incident with the same fingerprint already exists."""

    def __init__(self, message: str, existing_dir: Path):
        super().__init__(message)
        self.existing_dir = existing_dir


# ── failure.log parsing ─────────────────────────────────────────────────────

# lanista.cpp's own console format (native/tools/lanista.cpp): the status prefix is
# "PASS  "/"FAIL  " (two trailing spaces) or "INFRA " (one trailing space) - \s+ covers
# both without hard-coding the width. Detail (if any) is appended as "  [<detail>]" and
# evidence (if any, FAIL/INFRA only) as "  evidence: <path>" - see the format string at
# lanista.cpp ~line 698.
_STEP_LINE = re.compile(r"^(PASS|FAIL|INFRA)\s+(.*)$")
# The per-expect detail lanista itself formats as "<path> <op> <value> — got <actual>"
# (lanista.cpp ~line 681, QStringLiteral("%1 %2 %3 — got %4") - note the literal em-dash).
_DETAIL_SPLIT = re.compile(r"^(?P<path>\S+)\s+(?P<op>\S+)\s+(?P<value>.*?)\s+—\s+got\s+(?P<got>.*)$")
_EXIT_CODE_LINE = re.compile(r"^EXIT_CODE:\s*(-?\d+)\s*$")
_SUMMARY_LINE = re.compile(r"^(\d+) steps, (\d+) failed")

_INVOCATION_FLAGS_WITH_VALUE = {
    "--seed": "seed",
    "--tag": "tag",
    "--exe": "exe",
    "--qml": "qml",
    "--ready-ms": "readyMs",
}


@dataclass
class StepResult:
    index: int  # 1-based position among ALL step-result lines in the log
    status: str  # "PASS" | "FAIL" | "INFRA"
    label: str
    detail: str = ""
    evidence: str = ""  # path lanista reported for a failing step's auto-grab, if any

    @property
    def passed(self) -> bool:
        return self.status == "PASS"


@dataclass
class ParsedFailureLog:
    invocation: dict[str, Any]
    steps: list[StepResult]
    totalSteps: int
    failedCount: int
    exitCode: int | None


def _parse_invocation(line: str) -> dict[str, Any]:
    """Parse the `$ <command...>` header this slice's own minting convention requires
    (see module docstring, "Run-dir contract" -> failure.log). Step labels never appear on
    this line, so a plain shlex split (Windows-path-safe via posix=False) is enough."""
    if not line.startswith("$ "):
        raise MalformedRunDirError(
            "failure.log's first non-empty line must be the invocation header '$ <command>'"
            " - got: " + line[:200]
        )
    tokens = shlex.split(line[2:], posix=False)
    if not tokens:
        raise MalformedRunDirError("failure.log invocation header names no command")

    inv: dict[str, Any] = {
        "lanistaExe": tokens[0],
        "verb": None,
        "scenario": None,
        "seed": None,
        "tag": None,
        "exe": None,
        "qml": None,
        "readyMs": None,
        "drive": False,
        "verbose": False,
        "keepGoing": False,
    }
    rest = tokens[1:]
    positional: list[str] = []
    i = 0
    while i < len(rest):
        tok = rest[i]
        if tok == "--verbose":
            inv["verbose"] = True
            i += 1
        elif tok == "--drive":
            inv["drive"] = True
            i += 1
        elif tok == "--keep-going":
            inv["keepGoing"] = True
            i += 1
        elif tok in _INVOCATION_FLAGS_WITH_VALUE and i + 1 < len(rest):
            inv[_INVOCATION_FLAGS_WITH_VALUE[tok]] = _unquote(rest[i + 1])
            i += 2
        else:
            positional.append(_unquote(tok))
            i += 1

    if inv["readyMs"] is not None:
        try:
            inv["readyMs"] = int(inv["readyMs"])
        except ValueError:
            pass  # left as the raw string; not fatal to the packet

    if len(positional) >= 3 and positional[0] == "session" and positional[1] == "run":
        inv["verb"] = "session run"
        inv["scenario"] = positional[2]
    elif positional:
        inv["scenario"] = positional[-1]

    if not inv["scenario"]:
        raise MalformedRunDirError(
            "failure.log invocation header did not name a scenario file: " + line[:200]
        )
    return inv


def _unquote(token: str) -> str:
    if len(token) >= 2 and token[0] == token[-1] == '"':
        return token[1:-1]
    return token


def _split_step_line(status: str, rest: str) -> tuple[str, str, str]:
    """rest = 'LABEL[  [DETAIL]][  evidence: PATH]' (lanista.cpp's own construction
    order: detail bracket first, evidence second). DETAIL may itself contain nested
    '[...]' (the layout_verdict checkpoint joins per-rule brackets with ' | ') - split on
    the FIRST '  [' and the LAST ']' rather than searching for balanced brackets, which
    only holds because a step's own label text never itself contains the literal '  ['
    substring in any scenario this repo ships (verified against every FAIL/INFRA line
    lanista.cpp can emit)."""
    ev_idx = rest.rfind("  evidence: ")
    evidence = ""
    if ev_idx != -1:
        evidence = rest[ev_idx + len("  evidence: "):].strip()
        rest = rest[:ev_idx]
    br_idx = rest.find("  [")
    if br_idx != -1 and rest.endswith("]"):
        label = rest[:br_idx]
        detail = rest[br_idx + 3:-1]
    else:
        label, detail = rest, ""
    return label, detail, evidence


def parse_failure_log(text: str) -> ParsedFailureLog:
    lines = text.splitlines()
    non_empty = [ln for ln in lines if ln.strip()]
    if not non_empty:
        raise MalformedRunDirError("failure.log is empty")
    invocation = _parse_invocation(non_empty[0])

    steps: list[StepResult] = []
    exit_code: int | None = None
    summary_total: int | None = None
    summary_failed: int | None = None
    for line in lines:
        m = _STEP_LINE.match(line)
        if m:
            status, rest = m.group(1), m.group(2)
            label, detail, evidence = _split_step_line(status, rest)
            steps.append(StepResult(
                index=len(steps) + 1, status=status, label=label,
                detail=detail, evidence=evidence,
            ))
            continue
        m = _EXIT_CODE_LINE.match(line)
        if m:
            exit_code = int(m.group(1))
            continue
        m = _SUMMARY_LINE.match(line)
        if m:
            summary_total, summary_failed = int(m.group(1)), int(m.group(2))

    if not steps:
        raise MalformedRunDirError("failure.log carries no PASS/FAIL/INFRA step line")

    failed_count = sum(1 for s in steps if not s.passed)
    if summary_failed is not None and summary_failed != failed_count:
        raise MalformedRunDirError(
            f"failure.log's summary line says {summary_failed} failed but "
            f"{failed_count} FAIL/INFRA step line(s) were actually counted - "
            "the log is internally inconsistent"
        )
    total = summary_total if summary_total is not None else len(steps)

    return ParsedFailureLog(
        invocation=invocation, steps=steps, totalSteps=total,
        failedCount=failed_count, exitCode=exit_code,
    )


def _split_expected_got(detail: str) -> tuple[str | None, str | None]:
    if not detail:
        return None, None
    m = _DETAIL_SPLIT.match(detail)
    if not m:
        return detail, None
    expected = f"{m.group('path')} {m.group('op')} {m.group('value')}"
    return expected, m.group("got")


# ── A7 fingerprint + dedup ──────────────────────────────────────────────────


def compute_fingerprint(scenario: str, failing_label: str, expected: str, got: str) -> str:
    """A7: a stable hash of (scenario path, failing step label, expected, got). Uses a
    unit-separator join (never a printable delimiter that could itself appear inside one
    of the fields) so two different field splits can never collide onto the same digest."""
    basis = "\x1f".join([scenario or "", failing_label or "", expected or "", got or ""])
    return hashlib.sha256(basis.encode("utf-8")).hexdigest()


def _find_open_duplicate(artifacts_root: Path, fingerprint: str) -> Path | None:
    if not artifacts_root.exists():
        return None
    for incident_json in sorted(artifacts_root.glob("AR-*/incident.json")):
        try:
            obj = json.loads(incident_json.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(obj, dict) or obj.get("fingerprint") != fingerprint:
            continue
        incident_dir = incident_json.parent
        if "closedAt" in obj:
            continue  # forward-compatible escape hatch; unused by any slice today
        if (incident_dir / "CLOSED").exists():
            continue
        return incident_dir
    return None


def _next_incident_id(artifacts_root: Path, date_str: str) -> str:
    prefix = f"AR-{date_str}-"
    existing: list[int] = []
    if artifacts_root.exists():
        for p in artifacts_root.iterdir():
            if p.is_dir() and p.name.startswith(prefix) and p.name[len(prefix):].isdigit():
                existing.append(int(p.name[len(prefix):]))
    n = (max(existing) + 1) if existing else 1
    return f"{prefix}{n:04d}"


# ── best-effort shell-outs (git, warning gate) - never fatal to the packet ─


def _git(repo_root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", *args], cwd=str(repo_root),
            capture_output=True, text=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def _build_environment(repo_root: Path) -> dict[str, Any]:
    head = _git(repo_root, "rev-parse", "HEAD")
    status = _git(repo_root, "status", "--porcelain")
    dirty_files: list[str] = []
    if status:
        for line in status.splitlines():
            path = line[3:].strip() if len(line) > 3 else line.strip()
            if path:
                dirty_files.append(path)
    return {
        "headSha": head,
        "dirty": bool(dirty_files),
        "dirtyFileCount": len(dirty_files),
        "dirtyFiles": dirty_files,
        "toolchain": {
            "git": _git(repo_root, "--version"),
            "python": sys.version.split()[0],
        },
    }


def _run_warning_gate(repo_root: Path, log_paths: list[Path]) -> dict[str, Any]:
    script = repo_root / "tests" / "warning_gate.ps1"
    existing = [p for p in log_paths if p.is_file()]
    if not script.is_file():
        return {"invoked": False, "reason": "tests/warning_gate.ps1 not found"}
    if not existing:
        return {"invoked": False, "reason": "no log paths available in the packet to gate"}

    args = [
        "-NoProfile", "-NonInteractive", "-File", str(script),
        "-LogPath", ",".join(str(p) for p in existing),
    ]
    for shell in ("pwsh", "powershell"):
        try:
            result = subprocess.run(
                [shell, *args], capture_output=True, text=True, timeout=60,
            )
        except (OSError, subprocess.SubprocessError):
            continue
        output_lines = [ln for ln in result.stdout.splitlines() if ln.strip()]
        verdict = "WARNING_GATE_OK" if result.returncode == 0 else "FAIL"
        return {
            "invoked": True,
            "shell": shell,
            "exitCode": result.returncode,
            "verdict": verdict,
            "logPaths": [p.name for p in existing],
            "output": output_lines,
        }
    return {"invoked": False, "reason": "neither pwsh nor powershell is on PATH"}


def _scenario_touches_vault(scenario_rel: str, scenario_obj: dict[str, Any] | None) -> bool:
    if "vault" in scenario_rel.lower():
        return True
    if not scenario_obj:
        return False
    if "vault" in str(scenario_obj.get("name", "")).lower():
        return True
    for step in scenario_obj.get("steps", []):
        if not isinstance(step, dict):
            continue
        if step.get("cmd") == "vault-forensics":
            return True
        payload = step.get("payload")
        if isinstance(payload, dict):
            for v in payload.values():
                if isinstance(v, str) and "vault" in v.lower():
                    return True
    return False


def _reproduce_script(incident: dict[str, Any]) -> str:
    lines = [
        f"# Reproduce {incident['id']} (Colosseum Guardian Loop, Slice G3).",
        "# Generated by scripts/autorepair/incident.py - do not hand-edit. This is the",
        "# EXACT command that produced the red run (Fixture fragility law: embed exact",
        "# commands, never rely on ambient state).",
        '$ErrorActionPreference = "Stop"',
    ]
    exe = incident.get("lanistaExe") or "native/build-msvc/lanista.exe"
    parts = [f'& "{exe}"', "session", "run", f"\"{incident.get('scenario', '')}\""]
    if incident.get("exe"):
        parts += ["--exe", f"\"{incident['exe']}\""]
    if incident.get("qml"):
        parts += ["--qml", f"\"{incident['qml']}\""]
    if incident.get("tag"):
        parts += ["--tag", f"\"{incident['tag']}\""]
    if incident.get("seed"):
        parts += ["--seed", f"\"{incident['seed']}\""]
    if incident.get("drive"):
        parts.append("--drive")
    if incident.get("readyMs"):
        parts += ["--ready-ms", str(incident["readyMs"])]
    parts.append("--verbose")
    lines.append(" ".join(parts))
    return "\n".join(lines) + "\n"


# ── the builder ──────────────────────────────────────────────────────────────


@dataclass
class IncidentResult:
    id: str
    dir: Path
    incident: dict[str, Any]


def build_incident(
    run_dir: Path | str,
    *,
    artifacts_root: Path | str = DEFAULT_ARTIFACTS_ROOT,
    repo_root: Path | str = REPO_ROOT,
    now: datetime | None = None,
) -> IncidentResult:
    """Build one incident packet from `run_dir`. See the module docstring for the full
    run-dir contract and the D8 output file set. Never mutates `run_dir`."""
    run_dir = Path(run_dir)
    artifacts_root = Path(artifacts_root)
    repo_root = Path(repo_root)
    if now is None:
        now = datetime.now(timezone.utc)

    if not run_dir.is_dir():
        raise MalformedRunDirError(f"run dir not found: {run_dir}")

    session_path = run_dir / "session.json"
    if not session_path.is_file():
        raise MalformedRunDirError(f"missing session.json in run dir: {run_dir}")
    try:
        session = json.loads(session_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise MalformedRunDirError(f"session.json is not valid JSON: {exc}") from exc
    if not isinstance(session, dict):
        raise MalformedRunDirError("session.json must be a JSON object")

    failure_path = run_dir / "failure.log"
    if not failure_path.is_file():
        raise MalformedRunDirError(f"missing failure.log in run dir: {run_dir}")
    parsed = parse_failure_log(failure_path.read_text(encoding="utf-8"))

    if parsed.failedCount == 0:
        raise NoFailureError(
            f"run dir describes {parsed.totalSteps} step(s), all passing - "
            "no failure to report"
        )

    failing_step = next(s for s in parsed.steps if not s.passed)
    expected, got = _split_expected_got(failing_step.detail)

    scenario_rel = parsed.invocation.get("scenario") or ""
    scenario_obj: dict[str, Any] | None = None
    if scenario_rel:
        scenario_path = repo_root / scenario_rel
        if scenario_path.is_file():
            try:
                scenario_obj = json.loads(scenario_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                scenario_obj = None

    fingerprint = compute_fingerprint(scenario_rel, failing_step.label, expected or "", got or "")
    dup = _find_open_duplicate(artifacts_root, fingerprint)
    if dup is not None:
        raise DuplicateIncidentError(
            f"an OPEN incident with the same fingerprint already exists: {dup}", dup,
        )

    date_str = now.strftime("%Y-%m-%d")
    incident_id = _next_incident_id(artifacts_root, date_str)
    incident_dir = artifacts_root / incident_id
    incident_dir.mkdir(parents=True, exist_ok=False)

    # ---- copy the run dir's own logs verbatim (read-only input; never mutated) ----
    for name in ("failure.log", "stdout.log", "stderr.log", "colosseum.log"):
        src = run_dir / name
        if src.is_file():
            shutil.copy2(src, incident_dir / name)

    # ---- grabs: every *.png lanista already pulled into the run dir ----
    grab_paths = sorted(run_dir.glob("*.png"))
    if grab_paths:
        grabs_dir = incident_dir / "grabs"
        grabs_dir.mkdir()
        for p in grab_paths:
            shutil.copy2(p, grabs_dir / p.name)
    screen_src: Path | None = None
    if failing_step.evidence:
        candidate = run_dir / Path(failing_step.evidence).name
        if candidate.is_file():
            screen_src = candidate
    if screen_src is None and grab_paths:
        screen_src = grab_paths[-1]  # best-effort: the last grab taken in the run
    if screen_src is not None:
        shutil.copy2(screen_src, incident_dir / "screen.png")

    # ---- journey.json: the scenario file, read from the CURRENT tree (D1) ----
    if scenario_obj is not None:
        (incident_dir / "journey.json").write_text(
            json.dumps(scenario_obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    else:
        _write_json(incident_dir / "journey.json", {
            "available": False,
            "reason": f"scenario file not found in current tree: {scenario_rel!r}",
        })

    # ---- warnings.json: W0's verdict over whatever logs made it into the packet ----
    warning_result = _run_warning_gate(
        repo_root, [incident_dir / "colosseum.log", incident_dir / "stderr.log"],
    )
    _write_json(incident_dir / "warnings.json", warning_result)

    # ---- vault-forensics.json: only when the scenario touches a Vault surface ----
    vault_touching = _scenario_touches_vault(scenario_rel, scenario_obj)
    if vault_touching:
        _write_json(incident_dir / "vault-forensics.json", {
            "available": False,
            "reason": "scenario touches a Vault surface, but this is a post-mortem packet "
                      "- F1 vault-forensics requires a live session this builder has no "
                      "retroactive access to",
        })

    # ---- ui-tree.json: honest absence, never fabricated ----
    _write_json(incident_dir / "ui-tree.json", {
        "available": False,
        "reason": "post-mortem packet: dump-ui requires a live session; this builder runs "
                  "after the tagged session already exited",
    })

    # ---- environment.json ----
    environment = _build_environment(repo_root)
    _write_json(incident_dir / "environment.json", environment)

    # ---- incident.json ----
    incident: dict[str, Any] = {
        "schema": SCHEMA,
        "id": incident_id,
        "createdAt": now.isoformat(),
        "baseSha": environment.get("headSha"),
        "scenario": scenario_rel,
        "scenarioName": (scenario_obj or {}).get("name"),
        "seed": session.get("seedDir") or parsed.invocation.get("seed"),
        "tag": session.get("tag") or parsed.invocation.get("tag"),
        "exe": session.get("exe") or parsed.invocation.get("exe"),
        "qml": session.get("qml") or parsed.invocation.get("qml"),
        "lanistaExe": parsed.invocation.get("lanistaExe"),
        "drive": bool(session.get("drive", parsed.invocation.get("drive", False))),
        "readyMs": parsed.invocation.get("readyMs"),
        "sessionId": session.get("sessionId"),
        "runDir": str(run_dir),
        "exitCode": parsed.exitCode,
        "totalSteps": parsed.totalSteps,
        "failedCount": parsed.failedCount,
        "failingStep": {
            "index": failing_step.index,
            "label": failing_step.label,
            "status": failing_step.status,
            "detail": failing_step.detail,
            "expected": expected,
            "got": got,
        },
        "vaultTouching": vault_touching,
        "fingerprint": fingerprint,
    }
    _write_json(incident_dir / "incident.json", incident)

    # ---- reproduce.ps1 ----
    (incident_dir / "reproduce.ps1").write_text(_reproduce_script(incident), encoding="utf-8")

    return IncidentResult(id=incident_id, dir=incident_dir, incident=incident)


def _write_json(path: Path, obj: Any) -> None:
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


# ── CLI ──────────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build a Guardian Loop incident packet from a failed lanista run dir."
    )
    parser.add_argument("--from-run", required=True, metavar="RUN_DIR",
                         help="a run dir under artifacts/lanista-sessions/<id>/")
    args = parser.parse_args(argv)

    try:
        result = build_incident(args.from_run)
    except DuplicateIncidentError as exc:
        print(f"DUPLICATE INCIDENT: {exc}", file=sys.stderr)
        return 4
    except NoFailureError as exc:
        print(f"NO FAILURE: {exc}", file=sys.stderr)
        return 3
    except IncidentError as exc:
        print(f"INCIDENT ERROR: {exc}", file=sys.stderr)
        return 2

    fs = result.incident["failingStep"]
    print(f"INCIDENT {result.id}: {result.dir}")
    print(f"  failing step {fs['index']}/{result.incident['totalSteps']}: {fs['label']}")
    if fs.get("expected") is not None:
        print(f"    expected {fs['expected']} - got {fs['got']}")
    print(f"  fingerprint {result.incident['fingerprint']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
night_watch.py - the Night Watch (N0) + the G10 trigger slice.

docs/superpowers/plans/2026-08-15-colosseum-night-watch-n0-plan.md (this slice's
plan) and docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md Slice
G10 ("a FAILED nightly run opens an incident by itself"), which was blocked only
on N0 existing - this file is both.

What it does, in Hemanth's words: while he sleeps, the watch keeps opening the app
like a user and going through real journeys. Every journey that comes back green is
just logged. Every journey that comes back RED is captured exactly the way the
Guardian needs it, an incident is opened, and - gated by policy.nightWatchAutoRepair
- the Guardian is woken on it. With the shipped law's autonomyLevel at
"document-only", the Guardian triages, diagnoses, and writes bug.md: it documents
bugs, it does not fix them (Hemanth directive 2026-08-15: "only for documenting
bugs rather than fixing them for now").

Stdlib only (house pattern - scripts/autorepair/policy.py, scripts/soak-digest.py).
Shells out to: the built lanista.exe (scenario runs), taskkill (stray-app hygiene),
and - through the imported Guardian modules - whatever the live stage runners do in
their own sandboxes. Never touches the daily app: every session runs under a unique
disposable tag on its own pipe (Lanista's own isolation law).

The watch is SEQUENTIAL by design: while the Guardian works an incident (sandbox
build + triage + diagnosis), no new scenario runs - one machine, one build lane,
no RAM contention (the plan's Discipline section). A disk guard skips the Guardian
launch under 3 GB free rather than wedging the machine, and says so honestly in
the report.

Usage:
    python scripts/night_watch.py --dry-run              # show the admitted set + argv shapes
    python scripts/night_watch.py --duration-min 120     # a 2-hour watch
    python scripts/night_watch.py --once                 # one pass over the admitted set
    python scripts/night_watch.py --brain handoff ...    # GLM brain file handoff (default: GUARDIAN_BRAIN env, else claude)

Output: artifacts/night-watch/<UTC stamp>/watch-log.jsonl (one line per scenario
run) and report.md (the wake capstone - cycles, reds, incidents, terminal states,
bug docs). Exit code 0 unless the watch itself broke; red scenarios do NOT fail
the watch - they are its product.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

# scripts/night_watch.py -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[1]

# Sibling autorepair imports (house pattern: flat dir on sys.path, no package).
_AUTOREPAIR_DIR = REPO_ROOT / "scripts" / "autorepair"
if str(_AUTOREPAIR_DIR) not in sys.path:
    sys.path.insert(0, str(_AUTOREPAIR_DIR))

import live_runners  # noqa: E402  (build_live_stage_runners + set_model_backend)
import incident as incident_mod  # noqa: E402  (build_incident + DuplicateIncidentError)
import orchestrator  # noqa: E402  (run_incident)
from policy import load_policy  # noqa: E402

DEFAULT_ARTIFACTS_ROOT = REPO_ROOT / "artifacts" / "autorepair"
LANISTA_SESSIONS_DIR = REPO_ROOT / "artifacts" / "lanista-sessions"
NIGHT_WATCH_ROOT = REPO_ROOT / "artifacts" / "night-watch"

LANISTA_EXE = REPO_ROOT / "native" / "build-msvc" / "lanista.exe"
APP_EXE = REPO_ROOT / "native" / "build-msvc" / "colosseum.exe"

READY_MS = 90000  # the journeys' own generous app-ready bound (D10 rehearsal value)

# The admitted scenario set: ONLY families the supervised shakeout proved runnable
# head-to-head against the current build. Admission is evidence-first - a scenario
# that has not been shaken out does not run unsupervised. (The five journeys were
# proven green 2026-08-15 in the D10 baseline; other families join after their own
# shakeout pass updates this tuple.)
ADMITTED_SCENARIOS: tuple[str, ...] = (
    "tests/lanista_scenarios/journey_open_manga.json",
    "tests/lanista_scenarios/journey_play_video.json",
    "tests/lanista_scenarios/journey_vault_browse.json",
    "tests/lanista_scenarios/journey_ceremony.json",
    "tests/lanista_scenarios/journey_identify.json",
)

# Default per-family extra argv (beyond the common shape). The seed for a journey
# is resolved from the scenario's own comment at runtime (see _seed_for_scenario);
# this table carries only flags the comment cannot express. Empty by default -
# every journey today runs with the common shape + its comment-declared seed.
_FAMILY_EXTRA_ARGS: dict[str, list[str]] = {}

DISK_GUARD_BYTES = 3 * 1024**3  # skip the Guardian launch under 3 GB free

_FIXTURE_RE = re.compile(r"tests/lanista_fixtures/[A-Za-z0-9._/-]+")


class NightWatchError(RuntimeError):
    """The watch itself broke (missing lanista build, unwritable report dir, ...)."""


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _seed_for_scenario(scenario_path: Path) -> Path | None:
    """Resolve the scenario's fixture from its own comment: the first
    tests/lanista_fixtures/... path mentioned there, with a trailing /media/
    stripped (the fixture root is the seed, not its media subdir). A scenario
    whose comment names no fixture runs without --seed. Evidence source: the
    five journeys each name exactly their fixture root this way."""
    try:
        text = json.loads(scenario_path.read_text(encoding="utf-8")).get("comment", "")
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(text, str):
        return None
    m = _FIXTURE_RE.search(text)
    if not m:
        return None
    rel = m.group(0).rstrip("/")
    if rel.endswith("/media"):
        rel = rel[: -len("/media")]
    p = REPO_ROOT / rel
    return p if p.is_dir() else None


def scenario_argv(scenario: str, *, tag: str) -> list[str]:
    """The full invocation for one scenario run - the same shape the D10 rehearsal
    minted with, so failure.log's `$ <cmd>` header stays the exact repro line."""
    argv = [
        str(LANISTA_EXE), "session", "run", scenario,
        "--exe", str(APP_EXE), "--qml", "qml/Main.qml",
        "--tag", tag, "--drive",
        "--ready-ms", str(READY_MS), "--verbose",
    ]
    seed = _seed_for_scenario(REPO_ROOT / scenario)
    if seed is not None:
        argv += ["--seed", str(seed)]
    argv += _FAMILY_EXTRA_ARGS.get(scenario, [])
    return argv


def _kill_stray_app() -> None:
    """Hygiene between runs (Rule 17 spirit): a leaked colosseum.exe holds GPU
    textures and file handles into the next run. Best-effort, never fatal."""
    subprocess.run(
        ["taskkill", "/F", "/IM", "colosseum.exe"],
        capture_output=True, timeout=30,
    )


def _free_disk_bytes() -> int:
    return shutil.disk_usage(REPO_ROOT).free


def _snapshot_session_dirs() -> set[str]:
    if not LANISTA_SESSIONS_DIR.is_dir():
        return set()
    return {p.name for p in LANISTA_SESSIONS_DIR.iterdir() if p.is_dir()}


def _new_session_dir(before: set[str]) -> Path | None:
    now = _snapshot_session_dirs()
    new = sorted(now - before)
    return LANISTA_SESSIONS_DIR / new[-1] if new else None


def run_one_scenario(
    scenario: str,
    *,
    run_index: int,
    log_path: Path,
) -> dict[str, Any]:
    """One scenario execution + red capture. Returns the watch-log record."""
    stamp = _utc_now().strftime("%Y%m%d-%H%M%S")
    tag = f"nw-{stamp}-{run_index:03d}"
    argv = scenario_argv(scenario, tag=tag)
    before = _snapshot_session_dirs()
    started = time.monotonic()
    try:
        proc = subprocess.run(
            argv, cwd=REPO_ROOT, capture_output=True, text=True,
            timeout=READY_MS + 300, encoding="utf-8", errors="replace",
        )
        exit_code, stdout = proc.returncode, proc.stdout
    except subprocess.TimeoutExpired as exc:
        exit_code, stdout = 124, (exc.stdout or "" if isinstance(exc.stdout, str) else "")
    except OSError as exc:
        raise NightWatchError(f"could not launch lanista ({argv[0]!r}): {exc}") from exc
    elapsed = int(time.monotonic() - started)

    record: dict[str, Any] = {
        "ts": _utc_now().isoformat(), "scenario": scenario, "tag": tag,
        "exitCode": exit_code, "elapsedSec": elapsed, "red": exit_code != 0,
    }

    run_dir = _new_session_dir(before)
    record["runDir"] = str(run_dir) if run_dir else None
    if exit_code == 0:
        _append_log(log_path, record)
        _kill_stray_app()
        return record

    # ---- RED: capture for the incident builder (its own producer contract) ----
    if run_dir is not None:
        failure_log = run_dir / "failure.log"
        failure_log.write_text(
            "$ " + " ".join(argv) + "\n" + (stdout or ""), encoding="utf-8",
        )
    record["failureLog"] = str(run_dir / "failure.log") if run_dir else None
    _append_log(log_path, record)
    _kill_stray_app()
    return record


def open_incident(run_dir: Path) -> dict[str, Any]:
    """G10 step 1: mint the incident (dedup refused -> an honest 'duplicate' record,
    never a second copy of one flake)."""
    try:
        result = incident_mod.build_incident(run_dir, artifacts_root=DEFAULT_ARTIFACTS_ROOT)
    except incident_mod.DuplicateIncidentError as exc:
        return {"opened": False, "duplicate": True, "detail": str(exc)}
    except incident_mod.IncidentError as exc:
        return {"opened": False, "duplicate": False, "detail": str(exc)}
    return {"opened": True, "duplicate": False, "incidentId": result.id, "dir": str(result.dir)}


def run_guardian(incident_dir: Path, *, jobs: int = 1) -> dict[str, Any]:
    """G10 step 2 (policy-gated by the caller): run the loop on the incident. In
    document-only law this terminates DOCUMENTED with bug.md and never repairs.
    `jobs` is the sandbox build parallelism (a cold sandbox build at jobs=1 measured
    ~83 min on this machine on 2026-08-15; the triage stage cap was raised to 7200s
    to absorb exactly that)."""
    stage_runners = live_runners.build_live_stage_runners(jobs=jobs)
    try:
        outcome = orchestrator.run_incident(incident_dir, stage_runners=stage_runners)
    except orchestrator.OrchestratorError as exc:
        return {"terminalState": "ORCHESTRATOR-ERROR", "detail": str(exc)}
    return {
        "terminalState": outcome.get("terminalState"),
        "ranStages": outcome.get("ranStages"),
    }


def _append_log(log_path: Path, record: dict[str, Any]) -> None:
    with log_path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, ensure_ascii=False) + "\n")


def _write_report(
    report_dir: Path,
    *,
    started_at: datetime,
    ended_at: datetime,
    records: list[dict[str, Any]],
    incident_results: list[dict[str, Any]],
    guardian_results: list[dict[str, Any] | None],
    gate_on: bool,
    reason_stopped: str,
) -> Path:
    greens = [r for r in records if not r["red"]]
    reds = [r for r in records if r["red"]]
    lines = [
        "# Night Watch report",
        "",
        f"Window: {started_at.isoformat()} to {ended_at.isoformat()} ({reason_stopped}).",
        f"Runs: {len(records)} total, {len(greens)} green, {len(reds)} red.",
        f"Guardian gate (policy.nightWatchAutoRepair): {'on' if gate_on else 'off'}.",
        "",
    ]
    if records:
        lines.append("## Scenario outcomes")
        lines.append("")
        for r in records:
            mark = "RED" if r["red"] else "green"
            lines.append(
                f"- {r['ts']} {r['scenario']} - {mark} (exit {r['exitCode']}, "
                f"{r['elapsedSec']}s, tag {r['tag']})"
            )
        lines.append("")
    if incident_results:
        lines.append("## Incidents opened")
        lines.append("")
        for ir in incident_results:
            if ir.get("opened"):
                lines.append(f"- {ir['incidentId']} ({ir['dir']})")
            else:
                kind = "duplicate" if ir.get("duplicate") else "refused"
                lines.append(f"- not opened ({kind}): {ir.get('detail', '')}")
        lines.append("")
    if guardian_results:
        lines.append("## Guardian outcomes")
        lines.append("")
        for gr in guardian_results:
            if gr is None:
                continue
            lines.append(f"- terminal {gr.get('terminalState')}: {json.dumps(gr, ensure_ascii=False)}")
        lines.append("")
    if reds and not incident_results:
        lines.append(
            "Red runs occurred but no incidents were opened (gate off or capture "
            "failed) - see watch-log.jsonl for the run dirs."
        )
        lines.append("")
    path = report_dir / "report.md"
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    return path


def resolve_scenarios(globs: list[str] | None) -> list[str]:
    """The admitted set by default; --scenarios overrides with repo-relative globs
    (sorted, deduped, must match at least one file each - a typo'd glob is a hard
    error, never a silently empty watch)."""
    if not globs:
        return list(ADMITTED_SCENARIOS)
    import fnmatch
    resolved: set[str] = set()
    for g in globs:
        hits = {
            str(p.relative_to(REPO_ROOT)).replace("\\", "/")
            for p in REPO_ROOT.glob(g)
            if p.is_file()
        }
        if not hits:
            raise NightWatchError(f"--scenarios glob matched nothing: {g}")
        resolved |= hits
    return sorted(resolved)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Night Watch (N0): loop Lanista scenarios against the current "
        "build; capture reds as incidents; hand them to the Guardian (gated)."
    )
    bounds = parser.add_mutually_exclusive_group()
    bounds.add_argument("--duration-min", type=int, default=120, help="wall-clock bound (default 120)")
    bounds.add_argument("--cycles", type=int, help="fixed number of passes over the scenario set")
    bounds.add_argument("--once", action="store_true", help="one pass (same as --cycles 1)")
    parser.add_argument("--scenarios", nargs="+", metavar="GLOB", help="repo-relative globs overriding the admitted set")
    parser.add_argument("--pause-sec", type=int, default=20, help="pause between scenario runs (default 20)")
    parser.add_argument(
        "--brain", choices=["claude", "handoff"], default=None,
        help="model backend for Guardian stage calls (default: GUARDIAN_BRAIN env, else claude)",
    )
    parser.add_argument("--jobs", type=int, default=1,
                        help="sandbox build parallelism for Guardian cold builds (default 1)")
    parser.add_argument("--resume", metavar="INCIDENT_DIR",
                        help="run the Guardian on one existing incident dir and exit "
                             "(no scenarios are run; other watch flags are ignored)")
    parser.add_argument("--dry-run", action="store_true", help="print the plan, run nothing")
    args = parser.parse_args(argv)

    if not LANISTA_EXE.is_file():
        raise SystemExit(f"NightWatchError: lanista not built: {LANISTA_EXE}")
    if not APP_EXE.is_file():
        raise SystemExit(f"NightWatchError: app not built: {APP_EXE}")

    if args.brain:
        live_runners.set_model_backend(args.brain)

    if args.resume:
        incident_dir = Path(args.resume)
        if not (incident_dir / "incident.json").is_file():
            raise SystemExit(f"NightWatchError: not an incident dir (no incident.json): {incident_dir}")
        outcome = run_guardian(incident_dir, jobs=args.jobs)
        print(f"Guardian resume {incident_dir} -> {outcome.get('terminalState')}")
        if outcome.get("detail"):
            print(f"  detail: {outcome['detail']}")
        return 0 if outcome.get("terminalState") != "ORCHESTRATOR-ERROR" else 1

    scenarios = resolve_scenarios(args.scenarios)

    policy_obj = load_policy()
    gate_on = bool(policy_obj.policy["nightWatchAutoRepair"])

    started_at = _utc_now()
    report_dir = NIGHT_WATCH_ROOT / started_at.strftime("%Y%m%d-%H%M%S")
    log_path = report_dir / "watch-log.jsonl"

    if args.dry_run:
        print(f"Night Watch dry run - {len(scenarios)} scenario(s), gate {'on' if gate_on else 'off'}:")
        for s in scenarios:
            print("  $ " + " ".join(scenario_argv(s, tag="nw-DRYRUN")))
        return 0

    report_dir.mkdir(parents=True, exist_ok=True)
    print(f"Night Watch start {started_at.isoformat()}: {len(scenarios)} scenario(s), "
          f"gate {'on' if gate_on else 'off'}, backend {live_runners._resolved_backend()}")
    print(f"Report dir: {report_dir}")

    duration_sec = None if (args.cycles or args.once) else args.duration_min * 60
    max_cycles = 1 if args.once else args.cycles

    records: list[dict[str, Any]] = []
    incident_results: list[dict[str, Any]] = []
    guardian_results: list[dict[str, Any] | None] = []
    cycle = 0
    reason_stopped = "duration bound reached"
    watch_started = time.monotonic()

    while True:
        cycle += 1
        for i, scenario in enumerate(scenarios, start=1):
            print(f"[cycle {cycle}] ({i}/{len(scenarios)}) {scenario}")
            record = run_one_scenario(scenario, run_index=len(records) + 1, log_path=log_path)
            records.append(record)
            print(f"    -> {'RED' if record['red'] else 'green'} exit {record['exitCode']} "
                  f"({record['elapsedSec']}s)")
            if record["red"]:
                ir: dict[str, Any] | None = None
                gr: dict[str, Any] | None = None
                run_dir = record.get("runDir")
                if run_dir:
                    ir = open_incident(Path(run_dir))
                    incident_results.append(ir)
                    if ir.get("opened"):
                        if _free_disk_bytes() < DISK_GUARD_BYTES:
                            gr = {
                                "terminalState": "SKIPPED-LOW-DISK",
                                "detail": f"free bytes {_free_disk_bytes()} under guard {DISK_GUARD_BYTES}",
                            }
                        elif gate_on:
                            gr = run_guardian(Path(ir["dir"]), jobs=args.jobs)
                        else:
                            gr = {"terminalState": "GATE-OFF", "detail": "policy.nightWatchAutoRepair is false"}
                        guardian_results.append(gr)
                        print(f"    incident {ir['incidentId']} -> {gr.get('terminalState')}")
                    else:
                        guardian_results.append(None)
                        print(f"    incident not opened ({'duplicate' if ir.get('duplicate') else 'refused'}): {ir.get('detail', '')[:120]}")
                else:
                    incident_results.append({"opened": False, "duplicate": False, "detail": "no run dir captured"})
                    guardian_results.append(None)
            # duration bound is checked BETWEEN runs: a watch never cuts a scenario
            # mid-flight, it simply does not start another one past its window.
            if duration_sec is not None and time.monotonic() - watch_started >= duration_sec:
                break
            if i < len(scenarios) and args.pause_sec > 0:
                time.sleep(args.pause_sec)
        if max_cycles is not None:
            reason_stopped = f"{max_cycles} cycle(s) completed"
            if cycle >= max_cycles:
                break
        if duration_sec is not None and time.monotonic() - watch_started >= duration_sec:
            break

    ended_at = _utc_now()
    report = _write_report(
        report_dir, started_at=started_at, ended_at=ended_at, records=records,
        incident_results=incident_results, guardian_results=guardian_results,
        gate_on=gate_on, reason_stopped=reason_stopped,
    )
    print(f"Night Watch end {ended_at.isoformat()}: {len(records)} runs, "
          f"{sum(1 for r in records if r['red'])} red. Report: {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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
bug docs). The mind legs add: green-review/cycle-NN.json + green-reviews.jsonl
(per-cycle quality evidence and brain reads), synthesis-bundle.json (the whole
night in one file), and lacking-memo.md (the "where is the app lacking" memo;
handoff backend only, --green-review-timeout-sec 0 / --synthesis-timeout-sec 0
disable the calls while keeping every bundle). Exit code 0 unless the watch
itself broke; red scenarios do NOT fail the watch - they are its product.
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
    textures and file handles into the next run, and every colosseum boot spawns a
    stremio-runtime that OUTLIVES the app (live-proven 2026-08-15/16: one such
    orphan froze the Guardian's triage and later the watch itself by holding the
    child's stdout pipe). Best-effort, never fatal."""
    for image in ("colosseum.exe", "stremio-runtime.exe"):
        subprocess.run(
            ["taskkill", "/F", "/IM", image],
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
        # run_captured (not bare subprocess.run): the app under test spawns
        # stremio-runtime which inherits the stdout pipe and outlives lanista -
        # a pipe capture here froze the whole watch once (2026-08-16, 20:24 UTC).
        proc = live_runners.run_captured(
            argv, cwd=REPO_ROOT, timeout=READY_MS + 300,
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


# ══════════════════════════════════════════════════════════════════════════
# The mind legs (2026-08-16, Hemanth directive: "make the night watch have a
# more complex mind to figure out where the app is lacking"). The 2026-08-15
# night proved the gap empirically: 77 runs, every red a flake, zero confirmed
# product bugs - while the GREEN runs' own logs held the real signals (41 failed
# metahub requests, QRhi context drops, QSqlDatabase thread warnings) that no
# assertion looked at. These legs feed the brain (the GLM agent session, via the
# same file-handoff backend the Guardian's diagnosis stage uses) evidence the
# pass/fail harness cannot judge:
#   - green review: once per completed cycle, a quality judgment of that cycle
#   - night synthesis: at window close, the "where is the app lacking" memo
# Both are timeout-tolerant and honest: an unanswered call is recorded as
# unanswered, never fabricated, and the evidence bundles persist regardless so
# a morning brain can answer after the fact.
# ══════════════════════════════════════════════════════════════════════════

GREEN_REVIEWS_NAME = "green-reviews.jsonl"
SYNTHESIS_BUNDLE_NAME = "synthesis-bundle.json"
LACKING_MEMO_NAME = "lacking-memo.md"

# Quality-signal patterns from run stderr the assertions do not look at (each
# pattern below was a real, ignored-by-the-harness signal in the 2026-08-15
# night's logs).
_SIGNAL_RE = re.compile(
    r"(QRhi|QSqlDatabase|QIODevice|QML .?(Warning|Error)|failed=\d+|TIMEOUT|deadlock|"
    r"stutter|dropped frames|context current)",
    re.IGNORECASE,
)
_SIGNAL_LINES_PER_RUN = 20

GREEN_REVIEW_PROMPT = """You are the GREEN REVIEW mind of the Colosseum Night Watch.

The cycle bundle at {bundle_path} lists this cycle's runs. Most PASSED their assertions -
your job is NOT pass/fail. Judge where the app is LACKING: quality signals the assertions
cannot see. The bundle's "signals" field pre-extracts suspicious stderr lines; the run dirs
it names hold the full stdout.log/stderr.log/session.json - do your own tool work there
before claiming anything. Judge things like: warning storms, failed network requests on a
green run, graphics-context loss, SQL threading warnings, run-to-run elapsed variance,
anything that would embarrass us in front of a real user even though no step failed.

Answer with EXACTLY this JSON shape and nothing else (no prose, no markdown fence):
{{"findings": [{{"area": "<surface or subsystem>", "severity": "info|low|medium|high",
   "evidence": "<what you saw - cite the log line or file>"}}],
  "assessment": "<2-3 sentence overall quality read of this cycle>"}}

An EMPTY answer.json ({{}}) is the sanctioned no-findings answer. If "findings" is present
it must be a list of objects; anything malformed is recorded as invalid, never fatal."""

NIGHT_SYNTHESIS_PROMPT = """You are the NIGHT SYNTHESIS mind of the Colosseum Night Watch.

The whole night's evidence is bundled at {bundle_path}: every run with timings, the reds,
every incident and its terminal state, every green-review finding, and a signal histogram
across all cycles. Read it, then do your own tool work in the run dirs it names where you
need more than the bundle carries.

Write the "where is the app lacking" memo for this night. Answer with EXACTLY this JSON
shape and nothing else (no prose, no markdown fence):
{{"summary": "<what this night says about the app in 2-3 sentences>",
  "lacking": [{{"area": "<surface or subsystem>", "severity": "info|low|medium|high",
    "evidence": "<the night's evidence, cited>", "suggestion": "<what to look at next>"}}],
  "overall": "<one sentence: is the app getting more or less lacking, and why>"}}

An EMPTY answer.json ({{}}) is the sanctioned no-memo answer (the stub then points at the
bundle for a morning brain). "lacking", if present, must be a list of objects."""


def _extract_signals(run_dir: Path | None) -> list[str]:
    """Suspicious stderr lines from one run dir, deduped, capped. Green-run quality
    evidence: the 2026-08-15 night's real findings all lived in these lines."""
    if run_dir is None:
        return []
    stderr = Path(run_dir) / "stderr.log"
    if not stderr.is_file():
        return []
    try:
        lines = stderr.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []
    seen: set[str] = set()
    out: list[str] = []
    for line in lines:
        if _SIGNAL_RE.search(line):
            key = line.strip()
            if key not in seen:
                seen.add(key)
                out.append(key)
            if len(out) >= _SIGNAL_LINES_PER_RUN:
                break
    return out


def _run_summary(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "scenario": record["scenario"], "tag": record["tag"],
        "exitCode": record["exitCode"], "elapsedSec": record["elapsedSec"],
        "red": record["red"], "runDir": record.get("runDir"),
    }


def _validate_findings_shape(answer: dict[str, Any], list_key: str) -> str | None:
    """Loose validation, never fatal: returns an error string if a present list
    field is malformed (recorded honestly), else None."""
    items = answer.get(list_key)
    if items is None:
        return None
    if not isinstance(items, list) or not all(isinstance(x, dict) for x in items):
        return f"answer.{list_key} must be a list of objects"
    return None


def _green_review_brain_call(prompt: str, *, timeout_sec: int) -> dict[str, Any]:
    """Default brain call for the green review - the same file-handoff backend the
    Guardian's stages use (promoted for the watch by Hemanth's brain-reroute:
    the thinking brain is the GLM agent session)."""
    return live_runners.run_brain_file_handoff(
        prompt, cwd=REPO_ROOT, model="glm",
        allowed_tools=["Read", "Grep", "Glob", "Bash"],
        add_dirs=[REPO_ROOT], settings_path="night-watch (no sandbox - main repo)",
        timeout_sec=timeout_sec,
    )


def run_green_review(
    cycle: int,
    cycle_records: list[dict[str, Any]],
    report_dir: Path,
    *,
    brain_call=None,
    timeout_sec: int = 180,
    attempt_call: bool = True,
    skip_reason: str | None = None,
) -> dict[str, Any]:
    """One green review: bundle the cycle's evidence (always written - it is the
    record), then ask the brain for a quality read (timeout-bounded; an
    unanswered or invalid call is recorded honestly, never fatal to the watch).
    Returns the review record; appends it to green-reviews.jsonl."""
    review_dir = report_dir / "green-review"
    review_dir.mkdir(parents=True, exist_ok=True)
    bundle = {
        "cycle": cycle,
        "ts": _utc_now().isoformat(),
        "runs": [_run_summary(r) for r in cycle_records],
        "signals": {r["tag"]: _extract_signals(r.get("runDir")) for r in cycle_records},
    }
    bundle_path = review_dir / f"cycle-{cycle:02d}.json"
    bundle_path.write_text(json.dumps(bundle, indent=2, ensure_ascii=False), encoding="utf-8")

    record: dict[str, Any] = {
        "cycle": cycle, "bundle": str(bundle_path),
        "attempted": bool(attempt_call and timeout_sec > 0), "answered": False,
    }
    if not record["attempted"] and skip_reason and timeout_sec > 0:
        record["reason"] = skip_reason
    if record["attempted"]:
        prompt = GREEN_REVIEW_PROMPT.format(bundle_path=bundle_path)
        try:
            answer = (brain_call or _green_review_brain_call)(prompt, timeout_sec=timeout_sec)
            shape_error = _validate_findings_shape(answer, "findings")
            if shape_error:
                record.update(answered=False, invalid=shape_error)
            else:
                record.update(answered=True, findings=answer.get("findings", []),
                              assessment=answer.get("assessment", ""))
        except live_runners.ClaudeInvocationError as exc:
            record.update(answered=False, reason=str(exc)[:400])
    (report_dir / GREEN_REVIEWS_NAME).open("a", encoding="utf-8").write(
        json.dumps(record, ensure_ascii=False) + "\n"
    )
    return record


def _synthesis_brain_call(prompt: str, *, timeout_sec: int) -> dict[str, Any]:
    return live_runners.run_brain_file_handoff(
        prompt, cwd=REPO_ROOT, model="glm",
        allowed_tools=["Read", "Grep", "Glob", "Bash"],
        add_dirs=[REPO_ROOT], settings_path="night-watch (no sandbox - main repo)",
        timeout_sec=timeout_sec,
    )


def _write_synthesis_bundle(
    report_dir: Path,
    *,
    started_at: datetime, ended_at: datetime, reason_stopped: str,
    records: list[dict[str, Any]],
    incident_results: list[dict[str, Any]],
    guardian_results: list[dict[str, Any] | None],
    green_reviews: list[dict[str, Any]],
) -> Path:
    per_scenario: dict[str, dict[str, Any]] = {}
    for r in records:
        name = r["scenario"]
        stats = per_scenario.setdefault(
            name, {"runs": 0, "reds": 0, "elapsed": [], "exitCodes": {}}
        )
        stats["runs"] += 1
        stats["reds"] += 1 if r["red"] else 0
        stats["elapsed"].append(r["elapsedSec"])
        stats["exitCodes"][str(r["exitCode"])] = stats["exitCodes"].get(str(r["exitCode"]), 0) + 1
    for stats in per_scenario.values():
        e = stats.pop("elapsed")
        stats["elapsedMinSec"], stats["elapsedMaxSec"] = min(e), max(e)
    histogram: dict[str, int] = {}
    for r in records:
        for line in _extract_signals(r.get("runDir")):
            key = line[:80]
            histogram[key] = histogram.get(key, 0) + 1
    bundle = {
        "window": {"started": started_at.isoformat(), "ended": ended_at.isoformat(),
                   "reasonStopped": reason_stopped},
        "totals": {"runs": len(records), "reds": sum(1 for r in records if r["red"])},
        "perScenario": per_scenario,
        "runs": [_run_summary(r) for r in records],
        "incidents": [
            ir if not ir.get("opened")
            else {"opened": True, "incidentId": ir["incidentId"], "dir": ir["dir"]}
            for ir in incident_results
        ],
        "guardianOutcomes": [gr for gr in guardian_results if gr is not None],
        "greenReviews": green_reviews,
        "signalHistogram": dict(sorted(histogram.items(), key=lambda kv: -kv[1])),
    }
    path = report_dir / SYNTHESIS_BUNDLE_NAME
    path.write_text(json.dumps(bundle, indent=2, ensure_ascii=False), encoding="utf-8")
    return path


def run_night_synthesis(
    report_dir: Path,
    bundle_path: Path,
    *,
    brain_call=None,
    timeout_sec: int = 900,
    attempt_call: bool = True,
    skip_reason: str | None = None,
) -> Path:
    """The night's capstone: ask the brain for the lacking-memo. The memo file is
    ALWAYS written - from the answer when answered, as an honest stub (pointing
    at the bundle for a morning brain) when not."""
    memo_path = report_dir / LACKING_MEMO_NAME
    answer: dict[str, Any] = {}
    if timeout_sec <= 0:
        state = "disabled"
    elif not attempt_call:
        state = f"not attempted ({skip_reason})" if skip_reason else "not attempted"
    else:
        try:
            answer = (brain_call or _synthesis_brain_call)(
                NIGHT_SYNTHESIS_PROMPT.format(bundle_path=bundle_path),
                timeout_sec=timeout_sec,
            )
            if _validate_findings_shape(answer, "lacking"):
                answer, state = {}, "invalid"
            else:
                state = "answered"
        except live_runners.ClaudeInvocationError:
            state = "timeout"
    lines = ["# Where the app is lacking - Night Watch memo", ""]
    if state == "answered":
        lines.append(answer.get("summary", "").strip())
        lines.append("")
        for item in answer.get("lacking", []):
            lines.append(
                f"- [{str(item.get('severity', '?')).upper()}] {item.get('area', '?')} - "
                f"{item.get('evidence', '')} (next: {item.get('suggestion', '')})"
            )
        lines.append("")
        lines.append(f"Overall: {answer.get('overall', '').strip()}")
    else:
        lines.append(
            f"The brain did not answer this night ({state}). The full evidence is bundled at "
            f"`{bundle_path.name}` and per-cycle quality reads at `{GREEN_REVIEWS_NAME}` - "
            "summon the brain with those to write this memo after the fact."
        )
    memo_path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    return memo_path


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
    mind: dict[str, Path] | None = None,
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
    if mind:
        lines.append("## The mind's record (green review + night synthesis)")
        lines.append("")
        if memo := mind.get("memo"):
            lines.append(f"- Where the app is lacking: {Path(memo).name}")
        if reviews := mind.get("reviews"):
            answered = mind.get("reviewsAnswered", 0)
            total = mind.get("reviewsTotal", 0)
            lines.append(
                f"- Per-cycle quality reviews: {Path(reviews).name} "
                f"({answered}/{total} answered by the brain)"
            )
        if bundle := mind.get("bundle"):
            lines.append(f"- Whole-night evidence bundle: {Path(bundle).name}")
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
    parser.add_argument("--green-review-timeout-sec", type=int, default=180,
                        help="brain timeout per cycle's green review (default 180; "
                             "0 = write the evidence bundle only, no brain call)")
    parser.add_argument("--synthesis-timeout-sec", type=int, default=900,
                        help="brain timeout for the end-of-night lacking-memo call "
                             "(default 900; 0 = bundle only, stub memo)")
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
    green_reviews: list[dict[str, Any]] = []
    cycle = 0
    reason_stopped = "duration bound reached"
    watch_started = time.monotonic()

    # The mind legs fire only on the handoff backend (Hemanth's brain reroute:
    # the watch's thinking brain is the GLM agent session, reached by file
    # handoff). On the claude backend they still write every evidence bundle -
    # recorded as not attempted, never silently skipped.
    mind_attempt = live_runners._resolved_backend() == "handoff"

    while True:
        cycle += 1
        cycle_records: list[dict[str, Any]] = []
        for i, scenario in enumerate(scenarios, start=1):
            print(f"[cycle {cycle}] ({i}/{len(scenarios)}) {scenario}")
            record = run_one_scenario(scenario, run_index=len(records) + 1, log_path=log_path)
            records.append(record)
            cycle_records.append(record)
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
        # Green review fires only after a FULL cycle (a window cut mid-cycle has
        # its evidence folded into the night synthesis instead - reviewing a
        # partial cycle would judge a set the watch never finished running).
        if len(cycle_records) == len(scenarios):
            review = run_green_review(
                cycle, cycle_records, report_dir,
                timeout_sec=args.green_review_timeout_sec,
                attempt_call=mind_attempt,
                skip_reason=None if mind_attempt else "backend not handoff",
            )
            green_reviews.append(review)
            if review.get("answered"):
                print(f"    green review cycle {cycle}: {len(review.get('findings', []))} finding(s)")
            elif review.get("attempted"):
                print(f"    green review cycle {cycle}: not answered ({review.get('invalid') or review.get('reason') or 'no answer'})")
        if max_cycles is not None:
            reason_stopped = f"{max_cycles} cycle(s) completed"
            if cycle >= max_cycles:
                break
        if duration_sec is not None and time.monotonic() - watch_started >= duration_sec:
            break

    ended_at = _utc_now()
    synthesis_bundle = _write_synthesis_bundle(
        report_dir, started_at=started_at, ended_at=ended_at,
        reason_stopped=reason_stopped, records=records,
        incident_results=incident_results, guardian_results=guardian_results,
        green_reviews=green_reviews,
    )
    memo = run_night_synthesis(
        report_dir, synthesis_bundle,
        timeout_sec=args.synthesis_timeout_sec,
        attempt_call=mind_attempt,
        skip_reason=None if mind_attempt else "backend not handoff",
    )
    report = _write_report(
        report_dir, started_at=started_at, ended_at=ended_at, records=records,
        incident_results=incident_results, guardian_results=guardian_results,
        gate_on=gate_on, reason_stopped=reason_stopped,
        mind={
            "memo": memo,
            "reviews": report_dir / GREEN_REVIEWS_NAME,
            "reviewsAnswered": sum(1 for g in green_reviews if g.get("answered")),
            "reviewsTotal": len(green_reviews),
            "bundle": synthesis_bundle,
        },
    )
    print(f"Night Watch end {ended_at.isoformat()}: {len(records)} runs, "
          f"{sum(1 for r in records if r['red'])} red. Report: {report}")
    print(f"Lacking memo: {memo}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

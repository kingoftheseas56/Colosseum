#!/usr/bin/env python3
"""
Colosseum Guardian Loop - the orchestrator: the state machine that runs a failure from
incident to draft PR (Slice G9).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G9 ("The
orchestrator + the founding end-to-end run"). Decision D2 ("Orchestrator is Python 3
stdlib... a per-incident state machine over stage files; resumable; no daemon;
deterministic control flow in code, model calls only at named points"). This module is
that state machine's PURE half: sequencing, resume, budgets, locking, and terminal-state
report rendering. The LIVE half - actually calling a headless agent, building a sandbox,
running a build - is an injectable seam (`stage_runners`) whose DEFAULT raises
NotImplementedError, exactly the `default_run_once`/`default_invoke` pattern every sibling
slice (G4 triage.py, G5 diagnosis.py, G6 repair_contract.py, G7 verify.py, G8 promotion.py)
already established. This slice never reimplements any of those five modules' own logic -
it only sequences their PUBLIC ENTRY POINTS (triage.triage(), diagnosis.diagnose(),
repair_contract.run_repair(), verify.run_verify(), promotion.promote()) through the
`stage_runners` seam, and consumes their JSON-shaped outputs as opaque stage-file payloads.

Because none of those five calls happen inside this module's own code (they live entirely
behind the injected seam), this module does NOT import triage.py/diagnosis.py/
repair_contract.py/verify.py/promotion.py at all - only policy.py (the laws), sandbox.py
(for the DriftViolation type this module catches - ruling 7b), and incident.py (for
build_incident(), the CLI's `--from-run` entry point). See "DEFERRED" below for exactly
what a live `stage_runners[stage]` implementation would do, module by module.

Stage sequence (this slice's own instructions, verbatim): incident (G3) -> triage (G4) ->
diagnosis (G5) -> repair (G6) -> verify (G7) -> promotion (G8). Each of the five LOOP
stages (everything after "incident") writes its output as a JSON stage-file under
artifacts/autorepair/<id>/ (D8): triage.json, diagnosis.json, repair.json, verdict.json,
report.md. "incident" is not itself a loop stage with an injectable runner - it is the
PRECONDITION every run/resume call requires (incident.json must already exist, built via
incident.build_incident() at CLI `--from-run` time, or by a prior run) - see
run_incident()'s own docstring for why.

D8 naming note (an interpretation call, flagged here plainly): the plan's own file list
names "attempt-N/" as the repair stage's per-attempt evidence directory but does not name a
single JSON file for the repair stage's own completion the way triage.json/diagnosis.json/
verdict.json are named for their stages. This module writes `repair.json` (the FULL
repair_contract.run_repair() return value: accepted, attempts, attemptLog, classification,
bugtest, redExitCodes, greenExitCodes) as the repair stage's own stage-file - consistent
with every other stage, and the natural place a live stage_runners["repair"] implementation
records run_repair()'s own attemptLog (from which per-attempt attempt-N/ evidence
directories would be populated by that live implementation, one per attemptLog entry -
DEFERRED, not built by this slice). `verdict.json` (the verify stage's own file, D8's own
name) holds the FULL verify.run_verify() return value (stage/decision/approve/reasons/
riskAssessment/gates/refutation/verifierContext), a superset of verify.validate_verdict()'s
own {approve, reasons, riskAssessment} shape.

Resume rule (D2, this slice's own instructions, verbatim): "a rerun resumes at the first
incomplete stage - an already-written, valid stage file is not re-run; the first missing/
incomplete one and everything after it does run." This module's own reading of "everything
after it does run" (the mandatory negative control, below, is built to prove this
literally): once a stage actually re-runs because its own file was missing/invalid, EVERY
stage after it in the sequence re-runs too, even if that later stage's own file is still
present and individually valid on disk - a stale downstream file computed against a NOW-
DIFFERENT upstream result is never trusted just because it still parses. See
`first_incomplete_stage()` (a standalone pure helper) and `_run_loop()`'s own `force`
cascade flag.

Terminal states (this slice's own instructions, verbatim, with this module's own mapping
from stage outputs to each):

    PROMOTION-READY / PROMOTED   success - promotion.promote()'s own "mode" field decides
                                  which: mode="patch-only" -> PROMOTION-READY (no branch/PR,
                                  policy.autonomyLevel shakedown rung); mode="draft-pr" ->
                                  PROMOTED (a branch was pushed; the PR itself may still be
                                  "Bridge blocked" per promote()'s own honest gh-failure
                                  path - noted in the report body, never a silently-claimed
                                  success, never a 6th terminal state).
    ESCALATE                     diagnosis.wouldNeedForbiddenChange is true, OR diagnosis
                                  confidence is below policy.minConfidenceToRepair
                                  (diagnosis.mayProceedToRepair is false), OR the repair
                                  stage exhausted its attempts specifically because of an
                                  oversized patch (amendment A8 - detected by inspecting
                                  repair.json's own escalateReason text for "A8"/
                                  "oversized"), OR the independent Verifier REJECTED the
                                  patch (see the interpretation note below).
    BUDGET                       the repair stage exhausted policy.maxRepairAttempts
                                  without an accepted patch for a reason OTHER than an
                                  oversized patch (ordinary mechanical-gate rejections,
                                  or policy.maxRepairAttempts == 0), OR a single stage
                                  exceeded policy.perStageTimeoutSec[stage], OR the
                                  incident exceeded policy.perIncidentTotalSec (ruling 9).
    VIOLATION                    a stage runner raised sandbox.DriftViolation - the
                                  ruling-7b main-repo drift tripwire fired. Caught around
                                  the WHOLE stage sequence; whichever stage was executing
                                  when it fired is named in the report's detail text.
    TRIAGE-DISMISSED             triage.json's own "verdict" is not CONFIRMED (FLAKY,
                                  INFRA, or CONFIRMED-but-not-reproduced is impossible by
                                  triage.py's own D4 contract - FLAKY already covers the
                                  zero-failure "NOT-REPRODUCIBLE-AS-FILED" case, see
                                  triage.py's own triage() docstring) - no repair is ever
                                  attempted (ruling 3).

Interpretation call (flagged here loudly, not silently - this slice's own instructions ask
for it): the plan's G7 slice text says "Reject -> G6 retry loop with reasons," implying an
outer repair<->verify retry cycle. G9's OWN terminal-state list (given to this slice
verbatim) names exactly the five states above and does not name a distinct state for "the
Verifier rejected." No G9 test case requires a retry loop either. Rather than inventing an
un-specified orchestrator-level retry composition on top of two modules (repair_contract.py,
verify.py) that already each own a complete internal attempt loop against THEIR OWN
mechanical gates, this module implements a single repair-then-verify pass per incident
(matching the brief's own literal linear six-stage sequence) and maps a verify REJECT
straight to ESCALATE - the independent judge said no, and ruling 4 says "it is allowed and
expected to reject"; handing that to Hemanth rather than silently guessing a second sandbox
attempt is the conservative, defensible reading. This is called out again in the execution
report for gate confirmation. The live founding end-to-end run (D10, deferred) is where a
real reject-then-retry composition would first need to be built, if the ratified plan wants
one.

Single-flight `owner.lock` (the N0-spec pattern, this slice's own instructions): a lock file
under the incident directory carrying a pid/path/creation-time triple (JSON: {"pid": int,
"path": str, "createdAt": ISO8601 str}). acquire_lock() refuses (LockHeldError) a second
orchestrator instance for the SAME incident while the existing lock's pid is a LIVE process
(checked via `tasklist /FI "PID eq <pid>"` - the same house pattern sandbox.py's own
confirm_build_gate_clear() already uses, deliberately NOT `os.kill(pid, 0)`, which on
Windows actually calls TerminateProcess rather than merely probing liveness). A STALE lock
(dead pid) is silently reclaimed - overwritten with the new owner's own pid/path/
createdAt - never left to block forever. release_lock() only removes the lock file if it
still names the caller's own pid (never deletes a lock some other process has since
reclaimed).

Budgets (ruling 9): `_execute_stage()` is the ONE place a stage's wall-clock is measured,
via an injectable `clock` callable (default time.monotonic) - a stage whose measured
elapsed time exceeds `policy.perStageTimeoutSec[stage]` raises StageBudgetExceeded; the
running incident total (since the loop started) exceeding `policy.perIncidentTotalSec`
raises IncidentBudgetExceeded; both map to terminal BUDGET. This accounting is fully pure
given canned elapsed times (a test supplies a fake `clock` whose value a canned stage runner
advances directly - no real sleep, no real subprocess, exercising the SAME code path
production would use). Amendment A9 (Windows process hygiene - "stage timeouts kill the
full process tree, taskkill /T /F, never a bare process kill") is NOT reimplemented here:
this module's own `_execute_stage()` has no subprocess of its own to kill - the process tree
that might need killing belongs to whatever real work a LIVE `stage_runners[stage]`
implementation spawns (a sandbox build, a headless `claude -p` session), and
sandbox.py's own `destroy()`/`_kill_process_tree_rooted_in()` (Slice G2, already built and
tested) is the mechanism such a live implementation would reuse the moment its own
`subprocess.run(..., timeout=...)` raises TimeoutExpired - never reimplemented a second
time here. This module's own contribution to A9 is purely the POST-HOC cap enforcement
(reject a stage that measurably ran too long) plus this documentation of where the real
kill belongs.

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by this
slice per its own instructions), module by module - what a real `stage_runners[stage]`
entry would do:

  stage_runners["triage"]     -> call triage.triage(incident, policy_obj=policy_obj,
                                  run_once=triage.make_live_run_once(incident)) (Slice G4;
                                  triage.py's own live seam, already named and deferred
                                  there) and return its dict verbatim.
  stage_runners["diagnosis"]  -> call diagnosis.diagnose(incident, sandbox_root,
                                  policy_obj=policy_obj) with diagnosis.py's own
                                  default_invoke() replaced by the real headless-Opus
                                  invocation (Slice G5's own deferred seam) and return its
                                  dict verbatim.
  stage_runners["repair"]     -> call repair_contract.run_repair(incident, diagnosis,
                                  sandbox_root, policy_obj=policy_obj) with
                                  repair_contract.py's own default_invoke() replaced by the
                                  real headless-Sonnet attempt loop (Slice G6's own deferred
                                  seam) and return its dict verbatim; additionally, for each
                                  entry in the returned attemptLog, write an
                                  artifacts/autorepair/<id>/attempt-<n>/ evidence directory
                                  (patch.diff, contract.log, redgreen.log - the same shape
                                  G6's own evidence-artifact convention already uses at
                                  artifacts/autorepair/g6/attempt-*/).
  stage_runners["verify"]     -> call verify.run_verify(incident, patch, base_sha,
                                  sandbox_root, policy_obj=policy_obj) with verify.py's own
                                  default_invoke() replaced by the real second-sandbox
                                  build + mechanical gates + headless-Opus Verifier + GLM
                                  refutation (Slice G7's own deferred seam) and return its
                                  dict verbatim.
  stage_runners["promotion"]  -> assemble the D9 twelve-item dossier from the incident +
                                  diagnosis + repair + verify stage outputs already
                                  available via this module's own `prior` kwarg, then call
                                  promotion.promote(incident, patch, verdict, dossier,
                                  base_sha, policy_obj=policy_obj) with promotion.py's own
                                  default_invoke() replaced by the real git worktree/gh
                                  sequence (Slice G8's own deferred seam) and return its
                                  dict verbatim.

D10 red-validation risk (flagged prominently, per this slice's own instructions): the
founding end-to-end run's planted bug is a `readerReady`-binds-to-visibility regression in
ComicReaderShell.qml. Slice G3's own recon already found that `readerReady` resolves DECODE
ERRORS too (a corrupted-archive negative control did NOT fail it as expected) - so before
the batched live pass burns a cold sandbox build on this plant, IT MUST FIRST BE VERIFIED
LIVE that the planted binding actually produces a genuine red in `journey_open_manga`, not a
silent pass the same way the decode-error negative control silently passed. If it does not,
an alternative planted regression must be chosen. This module's own tests use ONLY canned,
already-classified stage data (CONFIRMED/FLAKY/etc. as plain Python dicts) - they never run
`journey_open_manga` and so cannot themselves discover or rule out this risk; it is carried
forward here as an explicit open item for the founding-e2e batch, not resolved by this
slice.

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,incident,triage,diagnosis,
repair_contract,verify,promotion}.py). No pip dependencies. This module's own I/O is: JSON
stage-file read/write, report.md read/write, the owner.lock file, and one `tasklist`
subprocess call per lock acquisition (pid liveness) - never a sandbox, never a build, never
a model call, never git/gh.

Public API:

    OrchestratorError, IncidentMissingError, LockHeldError,
    StageBudgetExceeded, IncidentBudgetExceeded          # named refusals
    STAGE_SEQUENCE, LOOP_STAGES, TERMINAL_STATES,
    STAGE_FILE_NAMES                                       # the sequencing vocabulary
    OwnerLock                                               # the pid/path/createdAt triple
    acquire_lock(incident_dir, *, pid=None, now=None,
                 pid_is_alive=...) -> OwnerLock              # pure given an injectable
                                                              # pid_is_alive; real tasklist
                                                              # by default
    release_lock(incident_dir, lock) -> None
    stage_file_path(incident_dir, stage) -> Path
    is_stage_complete(incident_dir, stage) -> bool           # pure given a stage file
    first_incomplete_stage(incident_dir) -> str | None        # pure given stage files
    DEFAULT_STAGE_RUNNERS                                     # every entry raises
                                                               # NotImplementedError
    _execute_stage(stage, runner_call, *, clock, total_start,
                   policy_obj, incident_id) -> dict            # pure given canned elapsed
                                                                # times via `clock`
    render_report(incident, terminal_state, detail,
                   stage_results, *, generated_at) -> str       # pure, Hemanth-language,
                                                                 # no emoji (self-checked)
    run_incident(incident_dir, *, policy_obj=None,
                 stage_runners=None, clock=time.monotonic,
                 now=..., lock_pid=None) -> dict                # the orchestration entry
    main(argv=None) -> int                                       # --from-run / --incident
                                                                  # --resume CLI

Usage:
    python scripts/autorepair/orchestrator.py --from-run artifacts/lanista-sessions/<id>
    python scripts/autorepair/orchestrator.py --incident AR-2026-08-14-0001 --resume
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

# scripts/autorepair/orchestrator.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARTIFACTS_ROOT = REPO_ROOT / "artifacts" / "autorepair"

# Sibling imports (house pattern: flat scripts/autorepair/, no package __init__.py - see
# sandbox.py's/triage.py's/diagnosis.py's/repair_contract.py's/verify.py's/promotion.py's
# identical sys.path setup). Deliberately NOT importing triage.py/diagnosis.py/
# repair_contract.py/verify.py/promotion.py themselves - see the module docstring: none of
# their public entry points are called by THIS module's own code, only by a live
# stage_runners[...] implementation (deferred). Program ruling 1: policy.py owns
# policy.perStageTimeoutSec/perIncidentTotalSec; this module only reads them via
# load_policy(), it never hard-codes or re-derives its own copy.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import Policy, load_policy  # noqa: E402  (after sys.path setup, by design)

import sandbox  # noqa: E402  (DriftViolation type - ruling 7b, actually caught below)
import incident as incident_mod  # noqa: E402  (build_incident() - the CLI's --from-run path)

__all__ = [
    "REPO_ROOT",
    "DEFAULT_ARTIFACTS_ROOT",
    "SCHEMA",
    "STAGE_SEQUENCE",
    "LOOP_STAGES",
    "TERMINAL_STATES",
    "STAGE_FILE_NAMES",
    "LOCK_FILE_NAME",
    "OrchestratorError",
    "IncidentMissingError",
    "LockHeldError",
    "StageBudgetExceeded",
    "IncidentBudgetExceeded",
    "OwnerLock",
    "acquire_lock",
    "release_lock",
    "stage_file_path",
    "is_stage_complete",
    "first_incomplete_stage",
    "DEFAULT_STAGE_RUNNERS",
    "render_report",
    "run_incident",
    "main",
]

SCHEMA = 1

# The orchestrator's own six-stage sequence (this slice's instructions, verbatim). LOOP_
# STAGES is the subset that actually has an injectable stage_runners[...] entry and a JSON
# stage-file; "incident" is the precondition every run/resume call requires (see
# run_incident()'s own docstring) - it has no stage_runners entry and no
# policy.perStageTimeoutSec cap (policy.py's own STAGE_NAMES enum names "build" there
# instead of "incident" - reserved for a live stage_runner's OWN internal sandbox-build
# sub-timing, e.g. inside triage/repair/verify's real implementations; never consumed by
# this module's own top-level per-stage cap logic, flagged in the execution report as an
# interpretation call).
LOOP_STAGES: tuple[str, ...] = ("triage", "diagnosis", "repair", "verify", "promotion")
STAGE_SEQUENCE: tuple[str, ...] = ("incident",) + LOOP_STAGES

TERMINAL_STATES: tuple[str, ...] = (
    "PROMOTION-READY",
    "PROMOTED",
    "ESCALATE",
    "BUDGET",
    "VIOLATION",
    "TRIAGE-DISMISSED",
)

STAGE_FILE_NAMES: dict[str, str] = {
    "triage": "triage.json",
    "diagnosis": "diagnosis.json",
    "repair": "repair.json",
    "verify": "verdict.json",
    "promotion": "report.md",
}

LOCK_FILE_NAME = "owner.lock"


class OrchestratorError(RuntimeError):
    """Base for every clean, named orchestrator refusal."""


class IncidentMissingError(OrchestratorError):
    """run_incident() was asked to run/resume a directory with no valid incident.json -
    this module never mints an incident packet itself (that is incident.py's own job, via
    the CLI's --from-run action, before run_incident() is ever called)."""


class LockHeldError(OrchestratorError):
    """The N0-spec single-flight lock: owner.lock is held by a LIVE process (a different
    pid than the caller's own) - refusing a second orchestrator instance for the same
    incident."""


class StageBudgetExceeded(OrchestratorError):
    """Ruling 9: a single stage's measured elapsed time exceeded
    policy.perStageTimeoutSec[stage]. Maps to terminal BUDGET."""


class IncidentBudgetExceeded(OrchestratorError):
    """Ruling 9: the incident's running total elapsed time exceeded
    policy.perIncidentTotalSec. Maps to terminal BUDGET."""


# ══════════════════════════════════════════════════════════════════════════
# JSON stage-file I/O (small, local - mirrors incident.py's own _write_json())
# ══════════════════════════════════════════════════════════════════════════


def _read_json(path: Path) -> Any | None:
    """Returns None (never raises) for a missing, unreadable, or unparsable file - every
    caller in this module treats "no valid JSON here" as "this stage is incomplete," never
    as a hard error; a genuinely malformed stage file is simply re-run, not fatal."""
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def _write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


# ══════════════════════════════════════════════════════════════════════════
# Stage-file validity + the resume rule
# ══════════════════════════════════════════════════════════════════════════


def _is_valid_incident_file(obj: Any) -> bool:
    return isinstance(obj, dict) and bool(obj.get("id"))


def _is_valid_triage_file(obj: Any) -> bool:
    return (
        isinstance(obj, dict)
        and obj.get("verdict") in ("CONFIRMED", "FLAKY", "INFRA")
        and isinstance(obj.get("runs"), list)
    )


def _is_valid_diagnosis_file(obj: Any) -> bool:
    if not isinstance(obj, dict):
        return False
    required = {
        "observed", "expected", "rootCause", "seam", "confidence", "proposedRepair",
        "wouldNeedForbiddenChange", "escalate", "escalateReason", "mayProceedToRepair",
    }
    return required <= set(obj) and obj.get("confidence") in ("low", "medium", "high")


def _is_valid_repair_file(obj: Any) -> bool:
    return isinstance(obj, dict) and isinstance(obj.get("accepted"), bool)


def _is_valid_verdict_file(obj: Any) -> bool:
    return isinstance(obj, dict) and obj.get("decision") in ("REJECT", "APPROVE")


def _is_valid_report_file(path: Path) -> bool:
    if not path.is_file():
        return False
    return bool(path.read_text(encoding="utf-8", errors="replace").strip())


# Every entry except "promotion" reads+parses JSON via _read_json(); "promotion" reads the
# raw report.md text file instead (D8's own naming - report.md, not a *.json).
_JSON_STAGE_VALIDATORS: dict[str, Callable[[Any], bool]] = {
    "incident": _is_valid_incident_file,
    "triage": _is_valid_triage_file,
    "diagnosis": _is_valid_diagnosis_file,
    "repair": _is_valid_repair_file,
    "verify": _is_valid_verdict_file,
}


def stage_file_path(incident_dir: Path | str, stage: str) -> Path:
    """The one canonical path for a stage's own stage-file - "incident" maps to
    incident.json (built by incident.py, not this module); every LOOP_STAGES entry maps to
    STAGE_FILE_NAMES[stage]."""
    incident_dir = Path(incident_dir)
    if stage == "incident":
        return incident_dir / "incident.json"
    if stage not in STAGE_FILE_NAMES:
        raise OrchestratorError(f"unknown stage {stage!r}; expected one of {STAGE_SEQUENCE}")
    return incident_dir / STAGE_FILE_NAMES[stage]


def is_stage_complete(incident_dir: Path | str, stage: str) -> bool:
    """Pure given the stage file's current on-disk content (the one piece of real I/O is
    reading that single file). "Complete" means: the file exists AND parses AND satisfies
    that stage's own minimal shape check - an existing-but-malformed file is NOT complete,
    exactly like a missing one, so a corrupted stage file is repaired by re-running, never
    silently trusted."""
    path = stage_file_path(incident_dir, stage)
    if stage == "promotion":
        return _is_valid_report_file(path)
    validator = _JSON_STAGE_VALIDATORS[stage]
    return validator(_read_json(path))


def first_incomplete_stage(incident_dir: Path | str) -> str | None:
    """D2's resume rule, as a standalone pure query: walks STAGE_SEQUENCE in order and
    returns the name of the first stage whose own file is missing/invalid. Returns None
    when every stage (including "promotion" - i.e. report.md) is already complete: the
    incident has already reached a terminal state, and a resume call has nothing left to
    do. This function is a QUERY only - it never runs a stage or writes a file itself; see
    _run_loop()'s own `force` cascade for how "the first incomplete one and everything
    after it does run" is actually enforced during a real run/resume call (this function
    alone only ever names the first gap, it does not itself express the cascade)."""
    incident_dir = Path(incident_dir)
    for stage in STAGE_SEQUENCE:
        if not is_stage_complete(incident_dir, stage):
            return stage
    return None


_REPORT_HEADER_RE = re.compile(
    r"^# Guardian Loop incident .+ - (?P<state>[A-Z][A-Z-]*)\s*$", re.MULTILINE
)


def _extract_terminal_marker(report_text: str) -> str | None:
    match = _REPORT_HEADER_RE.search(report_text)
    return match.group("state") if match else None


# ══════════════════════════════════════════════════════════════════════════
# owner.lock - single-flight, pid/path/createdAt triple (N0-spec pattern)
# ══════════════════════════════════════════════════════════════════════════


def _pid_is_alive(pid: int) -> bool:
    """Real liveness check via `tasklist` - the SAME house pattern sandbox.py's own
    confirm_build_gate_clear() already uses (Slice G2), deliberately NOT `os.kill(pid, 0)`:
    on Windows, os.kill's signal 0 is not a pure probe - CPython maps it onto
    TerminateProcess(pid, 0), which would actually kill a live process rather than merely
    check it. Fails SAFE: if tasklist itself cannot be run or errors, this returns True
    (assume alive) rather than risk reclaiming a lock that is, in fact, still held."""
    if pid <= 0:
        return False
    try:
        result = subprocess.run(
            ["tasklist", "/FI", f"PID eq {pid}", "/NH"],
            capture_output=True, text=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return True
    if result.returncode != 0:
        return True
    return str(pid) in result.stdout


@dataclass(frozen=True)
class OwnerLock:
    """The N0-spec pid/path/creation-time triple this slice's instructions name
    explicitly. `path` is the incident directory the lock protects (a string, for clean
    JSON round-tripping); `createdAt` is an ISO 8601 string."""

    pid: int
    path: str
    createdAt: str

    def to_dict(self) -> dict[str, Any]:
        return {"pid": self.pid, "path": self.path, "createdAt": self.createdAt}


def _read_lock(lock_path: Path) -> dict[str, Any] | None:
    if not lock_path.is_file():
        return None
    try:
        obj = json.loads(lock_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None  # a corrupt/unreadable lock is treated as absent -> reclaimable
    if not isinstance(obj, dict) or not isinstance(obj.get("pid"), int) or isinstance(obj.get("pid"), bool):
        return None
    return obj


def acquire_lock(
    incident_dir: Path | str,
    *,
    pid: int | None = None,
    now: datetime | None = None,
    pid_is_alive: Callable[[int], bool] = _pid_is_alive,
) -> OwnerLock:
    """
    Single-flight owner.lock (N0-spec pattern). A SECOND orchestrator instance for the
    SAME incident is REFUSED (LockHeldError) while an existing lock names a pid
    `pid_is_alive()` reports as still running. A STALE lock (a dead pid) is silently
    RECLAIMED - overwritten with the caller's own pid/path/createdAt. A lock already
    naming the caller's OWN pid is treated as re-entrant (the same orchestrator process
    resuming its own incident) and is also reclaimed (createdAt refreshed) rather than
    refused.

    `pid_is_alive` is the injectable seam a test uses to avoid depending on real process
    lifetimes when a fake pid is convenient; the DEFAULT (`_pid_is_alive`) is a real
    `tasklist` check, so calling this with no override against a REAL live/dead subprocess
    pid exercises the exact same code path production does.
    """
    incident_dir = Path(incident_dir).resolve()
    incident_dir.mkdir(parents=True, exist_ok=True)
    lock_path = incident_dir / LOCK_FILE_NAME
    my_pid = pid if pid is not None else os.getpid()
    my_now = now or datetime.now(timezone.utc)

    existing = _read_lock(lock_path)
    if existing is not None:
        existing_pid = existing["pid"]
        if existing_pid != my_pid and pid_is_alive(existing_pid):
            raise LockHeldError(
                f"owner.lock at {lock_path} is held by a live process (pid={existing_pid}, "
                f"path={existing.get('path')!r}, createdAt={existing.get('createdAt')!r}) - "
                "refusing a second orchestrator instance for the same incident"
            )
        # Stale (dead pid) or re-entrant (same pid): fall through and reclaim below.

    lock = OwnerLock(pid=my_pid, path=str(incident_dir), createdAt=my_now.isoformat())
    lock_path.write_text(json.dumps(lock.to_dict(), indent=2) + "\n", encoding="utf-8")
    return lock


def release_lock(incident_dir: Path | str, lock: OwnerLock) -> None:
    """Removes owner.lock ONLY if it still names `lock.pid` - never deletes a lock some
    other process has since reclaimed (e.g. after this caller's own lock went stale for
    some external reason). A no-op, not an error, if the lock file is already gone or
    already reassigned."""
    incident_dir = Path(incident_dir)
    lock_path = incident_dir / LOCK_FILE_NAME
    current = _read_lock(lock_path)
    if current is not None and current.get("pid") == lock.pid:
        lock_path.unlink(missing_ok=True)


# ══════════════════════════════════════════════════════════════════════════
# Deferred stage runners - the injectable seam, default raises loudly
# ══════════════════════════════════════════════════════════════════════════


def _deferred_stage_runner(stage: str) -> Callable[..., dict[str, Any]]:
    def _runner(
        incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *, prior: dict[str, Any]
    ) -> dict[str, Any]:
        raise NotImplementedError(
            f"default stage runner for {stage!r} is DEFERRED (Guardian Loop batched "
            "runtime pass) - pass an injected stage_runners[...] callable (a canned stage "
            "output dict) for deterministic use; see this module's own docstring "
            "'DEFERRED' section for the exact real invocation this stage stands in for. "
            f"incident={incident.get('id')!r}, incident_dir={incident_dir!r}, "
            f"prior stages available={sorted(prior)!r}"
        )

    return _runner


DEFAULT_STAGE_RUNNERS: dict[str, Callable[..., dict[str, Any]]] = {
    stage: _deferred_stage_runner(stage) for stage in LOOP_STAGES
}


# ══════════════════════════════════════════════════════════════════════════
# _execute_stage() - budget accounting, pure given canned elapsed times
# ══════════════════════════════════════════════════════════════════════════


def _execute_stage(
    stage: str,
    runner_call: Callable[[], dict[str, Any]],
    *,
    clock: Callable[[], float],
    total_start: float,
    policy_obj: Policy,
    incident_id: str | None,
) -> dict[str, Any]:
    """
    Calls `runner_call()` once, measuring elapsed wall-clock via `clock` (default
    time.monotonic; a test supplies a fake stepping clock instead - see the module
    docstring's own "Budgets" section). Raises StageBudgetExceeded if this ONE stage's
    elapsed time exceeds policy.perStageTimeoutSec[stage] (stages outside that dict, i.e.
    "incident", are never called through this function at all - see LOOP_STAGES), or
    IncidentBudgetExceeded if the RUNNING TOTAL since `total_start` exceeds
    policy.perIncidentTotalSec. Returns `runner_call()`'s own return value unchanged when
    both checks clear.
    """
    stage_start = clock()
    result = runner_call()
    elapsed = clock() - stage_start

    cap = policy_obj.policy["perStageTimeoutSec"].get(stage)
    if cap is not None and elapsed > cap:
        raise StageBudgetExceeded(
            f"stage {stage!r} exceeded its perStageTimeoutSec cap for incident "
            f"{incident_id!r} ({elapsed:.1f}s > {cap}s)"
        )

    total_elapsed = clock() - total_start
    incident_cap = policy_obj.policy["perIncidentTotalSec"]
    if total_elapsed > incident_cap:
        raise IncidentBudgetExceeded(
            f"incident {incident_id!r} exceeded perIncidentTotalSec after stage {stage!r} "
            f"({total_elapsed:.1f}s > {incident_cap}s)"
        )

    return result


# ══════════════════════════════════════════════════════════════════════════
# render_report() - the Hemanth-language terminal-state capstone (D8: report.md)
# ══════════════════════════════════════════════════════════════════════════

# Mirrors promotion.py's own _EMOJI_RE (Colosseum product rule: "no color, no emoji" -
# self-checked here too, the same "self-protection, mechanized" pattern policy.py's
# _assert_self_protection()/verify.py's find_forbidden_verifier_exhibits() already
# established for their own rulings).
_EMOJI_RE = re.compile(
    "[\U0001F300-\U0001FAFF\U00002600-\U000027BF\U0001F1E6-\U0001F1FF✀-➿☀-⛿]"
)

_STATE_SUMMARY: dict[str, str] = {
    "PROMOTION-READY": (
        "The repair is verified and ready to promote by hand. policy.autonomyLevel is "
        "patch-only, so no branch or draft PR was created automatically."
    ),
    "PROMOTED": (
        "A repair branch is ready for your review. Nothing merges without you - the "
        "draft PR (or the pushed branch, if opening the PR itself failed) is the only "
        "thing this run produced against the real repository."
    ),
    "ESCALATE": (
        "The machine stopped short of a repair and is handing this incident to you "
        "rather than guessing further."
    ),
    "BUDGET": (
        "The machine used up its allowed attempts or time on this incident and stopped "
        "rather than keep spending - nothing was silently retried past its budget."
    ),
    "VIOLATION": (
        "The machine detected an unexpected change in the main Colosseum repository "
        "during a sandboxed stage and stopped immediately. Nothing further ran."
    ),
    "TRIAGE-DISMISSED": (
        "The failure did not reproduce reliably enough to act on, so no repair was ever "
        "attempted."
    ),
}


def render_report(
    incident: dict[str, Any],
    terminal_state: str,
    detail: str,
    stage_results: dict[str, Any],
    *,
    generated_at: datetime,
) -> str:
    """
    Pure Markdown renderer for report.md, the capstone artifact every terminal state
    produces (this slice's own instructions). Hemanth-language: plain prose, no color, no
    emoji, no taglines - self-checked against `_EMOJI_RE` before returning, the same
    "refuse rather than return a leaking/dirty artifact" pattern this program already uses
    elsewhere (raises OrchestratorError rather than silently shipping a bad report).

    The H1 header line always contains the incident id AND the terminal state name
    verbatim (`# Guardian Loop incident <id> - <STATE>`) - this is both the human-readable
    headline and the machine-readable marker `_extract_terminal_marker()` reads back on a
    later "already terminal" short-circuit.
    """
    if terminal_state not in TERMINAL_STATES:
        raise OrchestratorError(
            f"unknown terminal state {terminal_state!r}; expected one of {TERMINAL_STATES}"
        )

    incident_id = incident.get("id", "<unknown incident>")
    scenario = incident.get("scenario", "<unknown scenario>")

    lines: list[str] = [
        f"# Guardian Loop incident {incident_id} - {terminal_state}",
        "",
        _STATE_SUMMARY.get(terminal_state, ""),
        "",
        f"Generated {generated_at.isoformat()}.",
        "",
        "## What happened",
        "",
        detail.strip() if detail else "(no further detail was recorded for this stop.)",
        "",
        "## Failing scenario",
        "",
        str(scenario),
        "",
        "## Stages completed before this incident stopped",
        "",
    ]

    completed = [stage for stage in LOOP_STAGES if stage in stage_results]
    if completed:
        for stage in completed:
            lines.append(f"- {stage}: recorded (see {STAGE_FILE_NAMES[stage]})")
    else:
        lines.append("- none - this incident stopped before any loop stage completed.")

    lines.append("")
    body = "\n".join(lines).rstrip() + "\n"

    found = _EMOJI_RE.findall(body)
    if found:
        raise OrchestratorError(
            "REFUSED (Hemanth-language: no color, no emoji, no taglines) - the rendered "
            f"report.md would contain emoji/pictographic character(s): {found!r}"
        )
    return body


def _promotion_detail(promotion_result: dict[str, Any]) -> str:
    mode = promotion_result.get("mode")
    if mode == "patch-only":
        return (
            "policy.autonomyLevel is patch-only - the repair is verified and its patch "
            "is recorded for this incident; no branch or draft PR was created "
            "automatically. Switch policy.autonomyLevel to draft-pr and re-run promotion "
            "for a real branch and draft PR, or apply the patch by hand."
        )
    pr_status = promotion_result.get("prStatus")
    target_branch = promotion_result.get("targetBranch")
    if pr_status == "created":
        return f"Draft PR opened on branch {target_branch}: {promotion_result.get('prUrl')}"
    return (
        f"The repair branch {target_branch} was pushed, but opening the draft PR itself "
        "failed (Bridge blocked) - the PR body was written to "
        f"{promotion_result.get('prBodyFilePath')} for you to open by hand."
    )


# ══════════════════════════════════════════════════════════════════════════
# The state machine
# ══════════════════════════════════════════════════════════════════════════


def _terminate(
    incident_dir: Path,
    incident_obj: dict[str, Any],
    state: str,
    ran_stages: list[str],
    now: Callable[[], datetime],
    *,
    detail: str,
    stage_results: dict[str, Any],
) -> dict[str, Any]:
    report_text = render_report(incident_obj, state, detail, stage_results, generated_at=now())
    (incident_dir / "report.md").write_text(report_text, encoding="utf-8")
    return {
        "incidentId": incident_obj.get("id"),
        "terminalState": state,
        "alreadyTerminal": False,
        "ranStages": list(ran_stages),
        "detail": detail,
    }


def _run_loop(
    incident_dir: Path,
    incident_obj: dict[str, Any],
    policy_obj: Policy,
    runners: dict[str, Callable[..., dict[str, Any]]],
    clock: Callable[[], float],
    now: Callable[[], datetime],
) -> dict[str, Any]:
    ran_stages: list[str] = []
    context: dict[str, Any] = {}
    incident_id = incident_obj.get("id")
    total_start = clock()
    force = False  # cascades: once a stage actually reruns, every stage after it reruns too

    def _obtain(stage: str) -> dict[str, Any]:
        nonlocal force
        if force or not is_stage_complete(incident_dir, stage):
            result = _execute_stage(
                stage,
                lambda: runners[stage](incident_obj, incident_dir, policy_obj, prior=dict(context)),
                clock=clock, total_start=total_start, policy_obj=policy_obj, incident_id=incident_id,
            )
            _write_json(stage_file_path(incident_dir, stage), result)
            ran_stages.append(stage)
            force = True
        else:
            result = _read_json(stage_file_path(incident_dir, stage))
        context[stage] = result
        return result

    try:
        # ---- triage (G4) ----
        triage_obj = _obtain("triage")
        if triage_obj["verdict"] != "CONFIRMED":
            return _terminate(
                incident_dir, incident_obj, "TRIAGE-DISMISSED", ran_stages, now,
                detail=(
                    f"triage verdict={triage_obj['verdict']!r} "
                    f"(reproduced={triage_obj.get('reproduced')!r}) - ruling 3: no code "
                    "change is possible until the failure reproduces in a clean disposable "
                    "copy, so no repair was attempted."
                ),
                stage_results=context,
            )

        # ---- diagnosis (G5) ----
        diagnosis_obj = _obtain("diagnosis")
        if diagnosis_obj.get("escalate") or not diagnosis_obj.get("mayProceedToRepair"):
            reason = diagnosis_obj.get("escalateReason") or (
                f"diagnosis confidence {diagnosis_obj.get('confidence')!r} is below "
                f"policy.minConfidenceToRepair={policy_obj.policy['minConfidenceToRepair']!r}"
            )
            return _terminate(
                incident_dir, incident_obj, "ESCALATE", ran_stages, now,
                detail=reason, stage_results=context,
            )

        # ---- repair (G6) ----
        repair_obj = _obtain("repair")
        if not repair_obj.get("accepted"):
            reason = repair_obj.get("escalateReason", "") or ""
            state = "ESCALATE" if ("A8" in reason or "oversized" in reason.lower()) else "BUDGET"
            return _terminate(
                incident_dir, incident_obj, state, ran_stages, now,
                detail=reason or "repair exhausted its attempt budget without an accepted patch.",
                stage_results=context,
            )

        # ---- verify (G7) ----
        verify_obj = _obtain("verify")
        if verify_obj.get("decision") != "APPROVE":
            return _terminate(
                incident_dir, incident_obj, "ESCALATE", ran_stages, now,
                detail=(
                    "the independent Verifier rejected the patch (ruling 4 - a different "
                    f"mind judged it, and it is allowed and expected to reject): "
                    f"{verify_obj.get('reasons')!r}"
                ),
                stage_results=context,
            )

        # ---- promotion (G8) ----
        # report.md does not exist yet at this point (run_incident() already checked this
        # before ever entering _run_loop()), so promotion always executes once reached -
        # it is never gated behind is_stage_complete()/the `force` cascade the way the
        # four JSON stages above are.
        promotion_result = _execute_stage(
            "promotion",
            lambda: runners["promotion"](incident_obj, incident_dir, policy_obj, prior=dict(context)),
            clock=clock, total_start=total_start, policy_obj=policy_obj, incident_id=incident_id,
        )
        ran_stages.append("promotion")
        context["promotion"] = promotion_result

        state = "PROMOTION-READY" if promotion_result.get("mode") == "patch-only" else "PROMOTED"
        return _terminate(
            incident_dir, incident_obj, state, ran_stages, now,
            detail=_promotion_detail(promotion_result), stage_results=context,
        )

    except sandbox.DriftViolation as exc:
        return _terminate(
            incident_dir, incident_obj, "VIOLATION", ran_stages, now,
            detail=str(exc), stage_results=context,
        )
    except (StageBudgetExceeded, IncidentBudgetExceeded) as exc:
        return _terminate(
            incident_dir, incident_obj, "BUDGET", ran_stages, now,
            detail=str(exc), stage_results=context,
        )


def run_incident(
    incident_dir: Path | str,
    *,
    policy_obj: Policy | None = None,
    stage_runners: dict[str, Callable[..., dict[str, Any]]] | None = None,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], datetime] = lambda: datetime.now(timezone.utc),
    lock_pid: int | None = None,
) -> dict[str, Any]:
    """
    The orchestration entry (D2): runs a fresh incident from stage 1, or resumes an
    in-progress one, entirely by reading the incident directory's own stage files -
    identical code path either way (there is no separate "fresh" vs "resume" function;
    "resume" is simply what calling this on a partially-complete directory does).

    Requires `incident_dir` to already contain a valid incident.json - built via
    incident.build_incident() at CLI `--from-run` time, or already present from a prior
    run/resume call. Raises IncidentMissingError otherwise; this function never mints an
    incident packet itself.

    If report.md already exists and is non-empty, the incident has already reached a
    terminal state - this call is a clean no-op (no lock is even acquired, no stage_runner
    is ever invoked) and returns `{"alreadyTerminal": True, "ranStages": [], ...}`
    immediately, with `terminalState` recovered from the existing report.md's own header
    line.

    Otherwise: acquires the single-flight owner.lock (LockHeldError if another live
    process already holds it), runs/resumes the stage sequence via `_run_loop()`, and
    releases the lock in a `finally` regardless of outcome.

    `policy_obj` defaults to the real committed docs/autorepair/ law (load_policy() with no
    arguments), exactly every sibling stage module's own default. `stage_runners` merges
    OVER `DEFAULT_STAGE_RUNNERS` (every entry of which raises NotImplementedError) - a
    caller supplies only the stages it wants to inject; any stage left un-injected still
    raises loudly rather than silently doing live work. `clock` and `now` are injectable
    for deterministic testing (see the module docstring's own "Budgets" section);
    `lock_pid` lets a test control the pid this call's own lock records, independent of
    `os.getpid()`.
    """
    incident_dir = Path(incident_dir)
    if policy_obj is None:
        policy_obj = load_policy()

    incident_obj = _read_json(incident_dir / "incident.json")
    if not _is_valid_incident_file(incident_obj):
        raise IncidentMissingError(
            f"no valid incident.json in {incident_dir} - cannot run or resume this incident "
            "(build one first via incident.build_incident() / the --from-run CLI action)"
        )

    report_path = incident_dir / "report.md"
    if _is_valid_report_file(report_path):
        return {
            "incidentId": incident_obj.get("id"),
            "terminalState": _extract_terminal_marker(
                report_path.read_text(encoding="utf-8", errors="replace")
            ),
            "alreadyTerminal": True,
            "ranStages": [],
            "detail": "incident already reached a terminal state; report.md exists and was not touched.",
        }

    runners = dict(DEFAULT_STAGE_RUNNERS)
    if stage_runners:
        runners.update(stage_runners)

    lock = acquire_lock(incident_dir, pid=lock_pid, now=now())
    try:
        return _run_loop(incident_dir, incident_obj, policy_obj, runners, clock, now)
    finally:
        release_lock(incident_dir, lock)


# ══════════════════════════════════════════════════════════════════════════
# CLI - --from-run (mint + run) and --incident ... --resume (continue)
# ══════════════════════════════════════════════════════════════════════════


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Colosseum Guardian Loop orchestrator (Slice G9): run a failure from "
        "incident to draft PR, or resume an in-progress one."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--from-run", metavar="RUN_DIR",
        help="build a new incident from a failed lanista run dir, then run the loop",
    )
    group.add_argument(
        "--incident", metavar="INCIDENT_ID",
        help="continue an existing incident (requires --resume)",
    )
    parser.add_argument(
        "--resume", action="store_true",
        help="required alongside --incident - there is no other supported mode yet",
    )
    parser.add_argument(
        "--artifacts-root", default=str(DEFAULT_ARTIFACTS_ROOT),
        help="artifacts/autorepair/ root (default: the real repo's own)",
    )
    args = parser.parse_args(argv)

    artifacts_root = Path(args.artifacts_root)

    if args.from_run:
        result = incident_mod.build_incident(args.from_run, artifacts_root=artifacts_root)
        incident_dir = result.dir
    else:
        if not args.resume:
            parser.error("--incident requires --resume (there is no other supported mode yet)")
        incident_dir = artifacts_root / args.incident
        if not incident_dir.is_dir():
            parser.error(f"no incident directory found: {incident_dir}")

    try:
        outcome = run_incident(incident_dir)
    except OrchestratorError as exc:
        print(f"ORCHESTRATOR ERROR: {exc}", file=sys.stderr)
        return 2

    print(f"INCIDENT {outcome['incidentId']}: {outcome['terminalState']}")
    if outcome.get("alreadyTerminal"):
        print("  (already terminal - nothing ran)")
    else:
        print(f"  ran stages: {outcome['ranStages']}")
    print(f"  report: {incident_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

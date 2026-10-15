#!/usr/bin/env python3
"""
Colosseum Guardian Loop - triage: reproduce or dismiss (Slice G4, decision D4).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G4 ("Triage -
reproduce or dismiss, and the headless-agent probe"). Program ruling 3: "Triage before
repair. No code change is possible until the failure reproduces in a clean disposable
copy. FLAKY/INFRA verdicts end the incident with a report, never an edit."

D4 (deviation from the spec's "Triage Agent", named plainly there and again here):
triage is CODE, not a model. Reproduce k times, count, classify. Rerunning and counting
is a script's job; a mind enters later, at Diagnosis (G5). This module is exactly that
script - it contains no model call anywhere, on either its pure or its orchestration side.

This module splits cleanly into two halves, per this slice's own instructions:

  1. A PURE, hermetic classifier - classify_triage() - that takes k already-collected
     RunResults plus policy.triage.{runs,confirmThreshold} and returns the D4 verdict.
     Zero I/O, zero subprocess, zero sandbox: every deterministic test in this slice
     exercises only this half, with canned RunResults it constructs by hand.

  2. An orchestration entry - triage() - that WOULD build/reuse the incident's sandbox
     (scripts/autorepair/sandbox.py, Slice G2) at incident['baseSha'], run reproduce.ps1
     (Slice G3's own output) k times, and feed each run's outcome through
     classify_triage(). The k LIVE reproduce runs are the DEFERRED batched-runtime step
     this slice's instructions call out explicitly - see "DEFERRED" below and
     default_run_once()'s docstring for the exact seam. triage()'s own live run loop is
     just `for i in range(k): run_once(i)`; it never calls sandbox.create()/build()/
     provision() directly, and neither does anything else in this file - only imported
     for type/seam purposes and for the (also-deferred, never-called-by-tests)
     make_live_run_once() factory below.

D4's three-way rule table (classify_triage()'s whole job):

    CONFIRMED  >= policy.triage.confirmThreshold runs FAIL at the SAME step (the most
               common failing-step label's count meets the threshold).
    FLAKY      failures land at DIFFERENT steps (no single step's count reaches the
               threshold), OR too few runs failed at all ("a mix of pass/fail below
               threshold" - D4's own wording, which literally covers zero failures too;
               see the "reproduced" flag note on triage() below for how the zero-failure
               case is surfaced honestly without inventing a 4th verdict value).
    INFRA      any run never reached the asserted step at all - a boot/session/isolation
               failure. This module treats ANY infra-status run in the k-run set as
               dispositive for the WHOLE triage (interpretation call, flagged in the G4
               execution report): a broken sandbox/environment is not evidence about the
               bug's flakiness, so it pre-empts the FAIL/PASS count entirely rather than
               being folded into the FLAKY bucket.

RunResult deliberately reuses lanista's OWN step-status vocabulary - PASS/FAIL/INFRA -
exactly as native/tools/lanista.cpp emits it and scripts/autorepair/incident.py already
parses it (see incident.py's _STEP_LINE regex). This is not a new vocabulary invented for
triage; it is the one real system's own words, carried straight through.

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,incident}.py). No pip
dependencies.

Public API:

    RunResult(status, stepLabel=None)                        # PASS | FAIL | INFRA
    classify_triage(run_results, *, runs, confirm_threshold) -> dict   # pure, hermetic
    triage(incident, *, policy_obj=None, run_once=default_run_once) -> dict  # orchestration
    default_run_once(index, incident, clone=None) -> RunResult          # DEFERRED, raises

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by
this slice per its own instructions):

  - The three LIVE reproduce runs against a real sandbox built from the golden incident's
    baseSha (default_run_once()'s real implementation: build/reuse the sandbox via
    sandbox.create()/build()/provision(), run reproduce.ps1, parse lanista's PASS/FAIL/
    INFRA step line for the incident's failingStep.label into a RunResult).
  - The INFRA demonstration: remove a runtime DLL from a scratch sandbox copy and confirm
    triage() lands on verdict INFRA.
  - The headless-agent probe (a separate deliverable of this slice, built in
    scripts/autorepair/hooks/guard.py + its own tests - not this file): a scripted
    `claude -p` invocation with the guard hook wired, asserting in-sandbox read succeeds,
    an escape attempt is refused, and the main-repo drift tripwire stays silent.

Usage (manual sanity check on canned data only - no sandbox, no live run; mirrors
policy.py's own `python scripts/autorepair/policy.py` pattern):

    python scripts/autorepair/triage.py
"""

from __future__ import annotations

import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

SCHEMA = 1

# Sibling import (house pattern: flat scripts/autorepair/, no package __init__.py - see
# sandbox.py's identical sys.path setup). Program ruling 1: policy.py owns
# policy.triage.{runs,confirmThreshold}; this module only reads them via load_policy(),
# it never hard-codes or re-derives its own copy of those numbers.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import Policy, load_policy  # noqa: E402  (after sys.path setup, by design)

# sandbox.py is imported ONLY for the deferred live-runner seam (make_live_run_once()'s
# docstring/type references below) and is NEVER called - no create()/build()/provision()
# call appears anywhere in this file. Deterministic tests never import sandbox at all.
import sandbox  # noqa: E402,F401  (deferred-seam reference only, see make_live_run_once())

__all__ = [
    "SCHEMA",
    "TriageError",
    "RunResult",
    "classify_triage",
    "triage",
    "default_run_once",
    "make_live_run_once",
]

_VALID_STATUSES = ("PASS", "FAIL", "INFRA")


class TriageError(RuntimeError):
    """A triage refusal: malformed RunResult input, or a run-count/threshold mismatch
    against policy.triage. Never raised for an ordinary FLAKY/INFRA/CONFIRMED verdict -
    those are successful classifications, not errors."""


# ── RunResult: one reproduce.ps1 run's outcome, in lanista's own vocabulary ────


@dataclass(frozen=True)
class RunResult:
    """One of the k reproduce runs triage() collects. `status` mirrors lanista's own
    per-step PASS/FAIL/INFRA vocabulary (native/tools/lanista.cpp; see
    scripts/autorepair/incident.py's _STEP_LINE for the same three literal words parsed
    from a real failure.log). `stepLabel` is the label of the step that produced this
    result - required for FAIL and INFRA (a FAIL/INFRA with no named step is malformed:
    triage cannot judge step-consistency on a nameless failure), always None for PASS."""

    status: str
    stepLabel: str | None = None

    def __post_init__(self) -> None:
        if self.status not in _VALID_STATUSES:
            raise TriageError(
                f"RunResult.status must be one of {_VALID_STATUSES}, got {self.status!r}"
            )
        if self.status == "PASS" and self.stepLabel is not None:
            raise TriageError("RunResult.stepLabel must be None when status is PASS")
        if self.status in ("FAIL", "INFRA") and not self.stepLabel:
            raise TriageError(
                f"RunResult.stepLabel is required (non-empty) when status is {self.status!r}"
            )

    def to_dict(self) -> dict[str, Any]:
        return {"status": self.status, "stepLabel": self.stepLabel}


# ── classify_triage(): the pure D4 classifier ───────────────────────────────


def classify_triage(
    run_results: list[RunResult], *, runs: int, confirm_threshold: int
) -> dict[str, Any]:
    """
    D4's whole rule table, pure and hermetic: no I/O, no subprocess, no model call.

    `runs` and `confirm_threshold` are policy.triage.runs/confirmThreshold (the caller -
    triage() below, or a test - is responsible for sourcing them from the loaded Policy;
    this function only validates and applies them). `run_results` must contain EXACTLY
    `runs` entries: a triage that collected fewer runs than policy demands is not a
    partial answer to trust, it is an incomplete one, so this fails closed with a named
    TriageError rather than classifying on a short set (interpretation call, flagged in
    the G4 execution report - the plan does not spell out short-set handling explicitly).

    Returns {"verdict": "CONFIRMED"|"FLAKY"|"INFRA", "runs": [...], "failingStepConsistency": {...}}.
    """
    if confirm_threshold > runs:
        raise TriageError(
            f"confirm_threshold ({confirm_threshold}) cannot exceed runs ({runs}) - "
            "mirrors policy.py's own policy.triage schema validation"
        )
    if len(run_results) != runs:
        raise TriageError(
            f"expected exactly {runs} run result(s) per policy.triage.runs, "
            f"got {len(run_results)}"
        )

    runs_echo = [r.to_dict() for r in run_results]

    infra_runs = [r for r in run_results if r.status == "INFRA"]
    if infra_runs:
        return {
            "verdict": "INFRA",
            "runs": runs_echo,
            "failingStepConsistency": {
                "applicable": False,
                "reason": (
                    f"{len(infra_runs)} of {runs} run(s) never reached the asserted step "
                    "(boot/session/isolation failure) - D4 INFRA pre-empts FAIL/PASS "
                    "counting entirely"
                ),
                "infraStepLabels": sorted({r.stepLabel for r in infra_runs}),
            },
        }

    fails = [r for r in run_results if r.status == "FAIL"]
    label_counts = Counter(r.stepLabel for r in fails)
    if label_counts:
        top_label, top_count = label_counts.most_common(1)[0]
    else:
        top_label, top_count = None, 0

    confirmed = top_count >= confirm_threshold
    verdict = "CONFIRMED" if confirmed else "FLAKY"

    return {
        "verdict": verdict,
        "runs": runs_echo,
        "failingStepConsistency": {
            "applicable": True,
            "label": top_label,
            "count": top_count,
            "ofFailures": len(fails),
            "totalRuns": runs,
            "distinctFailingSteps": len(label_counts),
            "confirmThreshold": confirm_threshold,
        },
    }


# ── triage(): the orchestration entry (live half DEFERRED) ─────────────────


def default_run_once(index: int, incident: dict[str, Any], clone: Path | None = None) -> RunResult:
    """
    DEFERRED (Guardian Loop batched-runtime pass, explicitly named by this slice's own
    instructions): the real live reproduce runner. Its eventual implementation would run
    incident_dir/reproduce.ps1 (Slice G3's own output) inside a sandbox clone built/reused
    via sandbox.create()/build()/provision() (Slice G2) at incident['baseSha'], then parse
    lanista's PASS/FAIL/INFRA step line for the incident's failingStep.label out of that
    run's stdout into a RunResult (the same parsing incident.py's parse_failure_log()
    already does for a post-mortem log - this deferred implementation would reuse it, not
    reinvent it).

    NOT implemented and NOT called by anything in this module's deterministic tests -
    calling this raises loudly rather than silently returning fabricated data, so the
    deferred boundary can never be crossed by accident. triage()'s own default parameter
    points here; every deterministic test supplies its OWN run_once callable instead.
    """
    raise NotImplementedError(
        "default_run_once() is the DEFERRED live reproduce runner (Guardian Loop "
        "batched-runtime pass) - pass an injected run_once callable (canned RunResults) "
        f"for deterministic use; index={index}, incident={incident.get('id')!r}, clone={clone!r}"
    )


def make_live_run_once(
    incident: dict[str, Any], *, sandbox_root: Path | str = sandbox.DEFAULT_SANDBOX_ROOT
) -> Callable[[int], RunResult]:
    """
    DEFERRED (batched-runtime pass) - the factory the real orchestrator would call to get
    a working run_once closure: build ONE sandbox reused across all k runs via
    sandbox.create(incident['baseSha'], incident['id'], sandbox_root=sandbox_root) +
    sandbox.build(clone) + sandbox.provision(clone), then return a closure that re-invokes
    reproduce.ps1 against that same built clone for each index and classifies its output.
    Referenced here ONLY so this module visibly imports and names sandbox.py's real API
    (per this slice's instructions) without ever calling it - this factory itself is never
    called by triage(), by any deterministic test, or by anything else in this file.
    """
    raise NotImplementedError(
        "make_live_run_once() is the DEFERRED sandbox-backed run_once factory "
        "(Guardian Loop batched-runtime pass) - not built by this slice; "
        f"would sandbox.create()/build()/provision() at sandbox_root={sandbox_root!r} "
        f"for incident={incident.get('id')!r}"
    )


def triage(
    incident: dict[str, Any],
    *,
    policy_obj: Policy | None = None,
    run_once: Callable[[int, dict[str, Any]], RunResult] = default_run_once,
) -> dict[str, Any]:
    """
    Program ruling 3 / D4's orchestration half: run reproduce.ps1 policy.triage.runs
    times via `run_once`, feed the results through classify_triage(). `run_once` is the
    injectable seam this slice's instructions call for - its DEFAULT (default_run_once)
    raises NotImplementedError, so calling triage() with no override never silently
    touches a sandbox; deterministic tests always pass their own canned run_once.

    `policy_obj` defaults to the REAL committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - this only READS JSON, it never builds or touches a
    sandbox, so it is safe to call from this module's own tests too.
    """
    if policy_obj is None:
        policy_obj = load_policy()

    k = policy_obj.policy["triage"]["runs"]
    confirm_threshold = policy_obj.policy["triage"]["confirmThreshold"]

    run_results = [run_once(i, incident) for i in range(k)]
    verdict_obj = classify_triage(run_results, runs=k, confirm_threshold=confirm_threshold)

    # D4's own wording folds "zero failures" into FLAKY ("a mix of pass/fail below
    # threshold" - 0 is trivially below any positive threshold). The plan's Slice G4
    # regression-path text separately calls the all-passing case
    # "NOT-REPRODUCIBLE-AS-FILED" rather than a bare FLAKY - rather than inventing a 4th
    # value in classify_triage()'s own contract (which this slice's instructions pin to
    # exactly CONFIRMED|FLAKY|INFRA), that distinction is surfaced here, one layer up, as
    # a plain boolean the orchestrator/report layer can read. Interpretation call, flagged
    # in the G4 execution report.
    any_failure_or_infra = any(r.status != "PASS" for r in run_results)

    return {
        **verdict_obj,
        "incidentId": incident.get("id"),
        "reproduced": any_failure_or_infra,
    }


# ── CLI (manual sanity check on canned data only) ───────────────────────────


def main(argv: list[str] | None = None) -> int:
    del argv
    canned = [
        RunResult(status="FAIL", stepLabel="regression: reopen — shelf card renders again"),
        RunResult(status="FAIL", stepLabel="regression: reopen — shelf card renders again"),
        RunResult(status="FAIL", stepLabel="regression: reopen — shelf card renders again"),
    ]
    result = classify_triage(canned, runs=3, confirm_threshold=2)
    print("TRIAGE (canned sanity check, no sandbox, no live run):")
    print(f"  verdict = {result['verdict']}")
    print(f"  failingStepConsistency = {result['failingStepConsistency']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Colosseum Guardian Loop - Diagnosis: why, with citations, before any edit (Slice G5).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G5 ("Diagnosis -
why, with citations, before any edit"). Purpose: separate "what happened" from "how to
change the code," in writing, so the repair is aimed and the verifier can later judge
intent against outcome. Decision D3 (model routing): Diagnosis = Opus, read-only tools
(Read/Grep/Glob), `--add-dir` = sandbox + incident dir + docs/encyclopedia/ (read the
subsystem guide FIRST - house law), NO Bash, NO web.

This module splits cleanly into pure, hermetic-testable pieces plus a deferred
orchestration seam - the same shape as triage.py (Slice G4) and policy.py (Slice G1):

  1. validate_diagnosis(obj) -> dict          - closed-schema gate for diagnosis.json,
                                                 mirroring policy.py's own fail-closed
                                                 style: unknown key -> refuse; missing
                                                 field -> refuse; wrong type -> refuse;
                                                 bad enum -> refuse. Zero I/O.
  2. check_citations(diagnosis, sandbox_root)  - the F0-contract citation check: every
                                                 cited file:line must be real, in-range,
                                                 and inside the sandbox. Filesystem I/O
                                                 only (reads the cited file's line count),
                                                 no model call, no subprocess.
  3. check_forbidden_escalation(diagnosis)     - the stop-law reflex, mechanized:
                                                 wouldNeedForbiddenChange: true ->
                                                 ESCALATE to Hemanth instead of proceeding.
                                                 Pure.
  4. may_proceed_to_repair(confidence, floor)  - the confidence gate: policy.
                                                 minConfidenceToRepair vs the diagnosis's
                                                 own confidence, low < medium < high. Pure.
  5. diagnose(incident, sandbox_root, ...)     - the orchestration entry that WOULD call
                                                 headless Opus via an injectable `invoke`
                                                 seam, then wires 1-4 together in order.
                                                 `invoke`'s DEFAULT (default_invoke) raises
                                                 NotImplementedError - see its own
                                                 docstring for the exact real invocation it
                                                 stands in for. This is the ONE live Opus
                                                 diagnosis run on the golden incident,
                                                 explicitly DEFERRED to the Guardian Loop's
                                                 batched runtime pass (named here, not
                                                 silent - owed by this slice per its own
                                                 instructions).

Every deterministic test in tests/test_autorepair_diagnosis.py exercises pieces 1-4
directly on canned data, plus 5 with an injected canned `invoke` - never a real sandbox,
never a real `claude -p` call.

diagnosis.json's own output contract (this slice's instructions, verbatim - note it names
exactly these seven keys, no `schema` field, unlike policy.json/incident.json's own
schema-versioned contracts; that is a deliberate deviation this module holds to rather
than inventing one, flagged in the execution report):

    {
      "observed": "<what actually happened>",
      "expected": "<what should have happened>",
      "rootCause": {"file": "<sandbox-relative path>", "line": <1-based int>, "claim": "<why>"},
      "seam": "<the interface/boundary where the bug lives>",
      "confidence": "high" | "medium" | "low",
      "proposedRepair": "<what the repair stage should do>",
      "wouldNeedForbiddenChange": <bool>
    }

Citation discipline reuses scripts/autorepair/hooks/guard.py's own A2 canonicalization
(expand vars/~, resolve to a real path incl. junctions/reparse points, strip `\\\\?\\`,
case-fold) before ever checking containment or existence - the same discipline the
containment guard hook itself applies, so a citation naming a path that LOOKS
sandbox-relative but resolves outside it (via `..`, an absolute path, or a junction) can
never even reach an is_file() check outside the sandbox it claims to cite.

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,incident,triage}.py). No
pip dependencies. This module performs no git/subprocess work of its own; check_citations
only reads a file's line count, and diagnose()'s only "live" action is delegated entirely
to the injected `invoke` callable.

Public API:

    DiagnosisError, DiagnosisSchemaError, CitationError    # named refusals
    validate_diagnosis(obj) -> dict                         # pure, hermetic
    check_citations(diagnosis, sandbox_root) -> None         # filesystem I/O only
    check_forbidden_escalation(diagnosis) -> dict            # pure
    may_proceed_to_repair(confidence, min_confidence) -> bool # pure
    diagnose(incident, sandbox_root, *, policy_obj=None, invoke=default_invoke) -> dict
    default_invoke(incident, sandbox_root, *, model) -> dict  # DEFERRED, raises

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by
this slice per its own instructions):

  - The ONE live headless-Opus diagnosis run on the golden incident (G3's minted packet),
    whose citations must all resolve - see default_invoke()'s docstring for the exact
    real invocation (headless `claude -p`, read-only tools, `--add-dir` scoped to sandbox
    + incident dir + docs/encyclopedia/, guard hook wired) it stands in for.
  - The sandbox-drift tripwire check for THIS stage specifically ("sandbox is read-only in
    this stage - drift tripwire on the SANDBOX too, no edits before Repair" - this slice's
    own "Behavior to preserve" line): sandbox.py's main_drift_snapshot()/check() already
    exist (Slice G2) and diagnose()'s real orchestrator wiring would snapshot the sandbox
    before/after the invoke() call; this module does not call sandbox.py at all (Diagnosis
    reads evidence already captured in the incident packet and the sandbox tree - it does
    not itself decide when to snapshot the sandbox, the orchestrator does, exactly as
    triage.py never calls sandbox.create()/build() either).

Usage (manual sanity check on canned data only - no sandbox, no live run; mirrors
policy.py's/triage.py's own `python scripts/autorepair/<module>.py` pattern):

    python scripts/autorepair/diagnosis.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Callable

# scripts/autorepair/diagnosis.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling imports (house pattern: flat scripts/autorepair/, no package __init__.py - see
# triage.py's/sandbox.py's identical sys.path setup). Program ruling 1: policy.py owns
# policy.modelRouting.diagnosis and policy.minConfidenceToRepair; this module only reads
# them via load_policy(), it never hard-codes or re-derives its own copy. guard.py's
# canonicalize() is reused (not reimplemented) for check_citations()'s path discipline -
# the exact "same discipline as the guard hook" this slice's instructions call for.
_THIS_DIR = Path(__file__).resolve().parent
_HOOKS_DIR = _THIS_DIR / "hooks"
for _p in (_THIS_DIR, _HOOKS_DIR):
    _p_str = str(_p)
    if _p_str not in sys.path:
        sys.path.insert(0, _p_str)

from policy import CONFIDENCE_LEVELS, Policy, load_policy  # noqa: E402  (after sys.path setup)
from guard import canonicalize  # noqa: E402  (A2 discipline, reused not reimplemented)

__all__ = [
    "REPO_ROOT",
    "DiagnosisError",
    "DiagnosisSchemaError",
    "CitationError",
    "ESCALATE",
    "PROCEED",
    "validate_diagnosis",
    "check_citations",
    "check_forbidden_escalation",
    "may_proceed_to_repair",
    "diagnose",
    "default_invoke",
]

DIAGNOSIS_KEYS = {
    "observed",
    "expected",
    "rootCause",
    "seam",
    "confidence",
    "proposedRepair",
    "wouldNeedForbiddenChange",
}
ROOT_CAUSE_KEYS = {"file", "line", "claim"}

# low < medium < high - the confidence gate's whole ordering (may_proceed_to_repair()).
CONFIDENCE_ORDER = {"low": 0, "medium": 1, "high": 2}

ESCALATE = "ESCALATE"
PROCEED = "PROCEED"


class DiagnosisError(RuntimeError):
    """Base for every clean, named Diagnosis-stage refusal."""


class DiagnosisSchemaError(DiagnosisError):
    """diagnosis.json failed closed-schema validation: unknown key, missing field, wrong
    type, or a bad enum value. Mirrors policy.py's PolicySchemaError - same fail-closed
    style, own error type (this module's schema is diagnosis.json's, not policy's)."""


class CitationError(DiagnosisError):
    """rootCause.file/line does not resolve to a real, in-sandbox citation - the F0-
    contract citation check (this slice's own instructions: "every cited file:line must
    exist in the sandbox"). Raised for: a nonexistent file, an out-of-range line number, a
    '..'-bearing or absolute path, or a citation that canonicalizes outside sandbox_root
    entirely (a junction/symlink escape - guard.py's own A2 discipline, reused here so a
    citation can never even reach an is_file() check outside the sandbox it claims to
    cite)."""


# ── local closed-schema helpers (mirror policy.py's require_*, own error type) ─────
#
# policy.py's own require_object/require_string/require_bool/require_positive_int/
# require_enum/require_exact_keys are NOT imported directly: they raise PolicySchemaError,
# and this slice's contract requires every diagnosis.json violation to raise
# DiagnosisSchemaError by name. Reusing policy.py's error TYPE across two unrelated
# schemas (policy.json's law vs diagnosis.json's model output) would blur exactly the
# distinction a caller needs to catch one without the other. The validation SHAPE is
# copied verbatim (same fail-closed pattern, same messages style) - only the raised type
# differs, which is the whole point of keeping this a separate, small copy.


def _require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DiagnosisSchemaError(f"{context} must be an object")
    return value


def _require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise DiagnosisSchemaError(f"{context} must be a non-empty string")
    return value


def _require_bool(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise DiagnosisSchemaError(f"{context} must be a boolean")
    return value


def _require_positive_int(value: Any, context: str) -> int:
    # bool is a subclass of int in Python; reject it explicitly so `true`/`false` can
    # never silently satisfy an integer field (same guard as policy.py's own).
    if isinstance(value, bool) or not isinstance(value, int):
        raise DiagnosisSchemaError(f"{context} must be an integer")
    if value <= 0:
        raise DiagnosisSchemaError(f"{context} must be a positive integer, got {value}")
    return value


def _require_enum(value: Any, allowed: set[str], context: str) -> str:
    text = _require_string(value, context)
    if text not in allowed:
        raise DiagnosisSchemaError(
            f"{context} must be one of {', '.join(sorted(allowed))}; got {text!r}"
        )
    return text


def _require_exact_keys(obj: dict[str, Any], allowed: set[str], context: str) -> None:
    """Closed-schema field set: no unknown keys, no missing required keys."""
    unknown = set(obj) - allowed
    if unknown:
        raise DiagnosisSchemaError(
            f"{context} contains unknown field(s): " + ", ".join(sorted(unknown))
        )
    missing = allowed - set(obj)
    if missing:
        raise DiagnosisSchemaError(
            f"{context} missing required field(s): " + ", ".join(sorted(missing))
        )


# ── 1. validate_diagnosis(): the pure closed-schema gate ───────────────────────────


def validate_diagnosis(obj: Any) -> dict[str, Any]:
    """
    diagnosis.json's closed-schema gate (Slice G5's own output contract, verbatim - see
    the module docstring). Pure: no I/O, no filesystem, no sandbox - just structural and
    type validation of an already-parsed dict. Returns a fresh, normalized dict on
    success; raises DiagnosisSchemaError naming the exact offending field on any
    violation (unknown key, missing field, wrong type, or bad enum value).
    """
    diag = _require_object(obj, "diagnosis")
    _require_exact_keys(diag, DIAGNOSIS_KEYS, "diagnosis")

    observed = _require_string(diag["observed"], "diagnosis.observed")
    expected = _require_string(diag["expected"], "diagnosis.expected")
    seam = _require_string(diag["seam"], "diagnosis.seam")
    proposed_repair = _require_string(diag["proposedRepair"], "diagnosis.proposedRepair")
    confidence = _require_enum(diag["confidence"], CONFIDENCE_LEVELS, "diagnosis.confidence")
    would_need_forbidden_change = _require_bool(
        diag["wouldNeedForbiddenChange"], "diagnosis.wouldNeedForbiddenChange"
    )

    root_cause_raw = _require_object(diag["rootCause"], "diagnosis.rootCause")
    _require_exact_keys(root_cause_raw, ROOT_CAUSE_KEYS, "diagnosis.rootCause")
    root_cause = {
        "file": _require_string(root_cause_raw["file"], "diagnosis.rootCause.file"),
        "line": _require_positive_int(root_cause_raw["line"], "diagnosis.rootCause.line"),
        "claim": _require_string(root_cause_raw["claim"], "diagnosis.rootCause.claim"),
    }

    return {
        "observed": observed,
        "expected": expected,
        "rootCause": root_cause,
        "seam": seam,
        "confidence": confidence,
        "proposedRepair": proposed_repair,
        "wouldNeedForbiddenChange": would_need_forbidden_change,
    }


# ── 2. check_citations(): the F0-contract citation check ───────────────────────────


def _is_contained(canon_target: str, canon_root: str) -> bool:
    """Mirrors scripts/autorepair/hooks/guard.py's own private _is_contained() (A2
    discipline) - reimplemented locally rather than reaching into guard.py's un-exported
    internals (canonicalize() is guard.py's one public, __all__-exported seam; this
    three-line containment check is cheap enough to keep local rather than couple to
    another module's private symbol)."""
    if canon_target == canon_root:
        return True
    sep = os.sep.lower()
    return canon_target.startswith(canon_root.rstrip(sep) + sep)


def check_citations(diagnosis: dict[str, Any], sandbox_root: Path | str) -> None:
    """
    The F0-contract citation check (this slice's own instructions: "every cited file:line
    must exist in the sandbox"). `diagnosis` must already be validate_diagnosis()'s OWN
    return value (rootCause.line is a real positive int by the time this runs - this
    function trusts that contract and re-validates citations only, not types).

    Canonicalizes both sandbox_root and the cited file (guard.py's own A2 discipline -
    expand vars/~, resolve to a real path incl. junctions/reparse points, strip
    `\\\\?\\`, case-fold) and CONTAINS the citation inside sandbox_root before ever
    calling is_file() on it, so a citation naming an absolute path, a '..' escape, or a
    junction planted inside the sandbox that resolves outside it can never be mistaken
    for a real in-sandbox citation. Raises CitationError on any violation; returns None
    (no exception) on a clean citation - mirrors sandbox.py's own
    main_drift_check()/_assert_no_remotes() void-on-success, raise-on-failure shape.
    """
    root = Path(sandbox_root)
    canon_root = canonicalize(str(root))

    root_cause = diagnosis["rootCause"]
    cited_file = root_cause["file"]
    cited_line = root_cause["line"]

    raw = cited_file.strip().replace("\\", "/")
    if not raw:
        raise CitationError("rootCause.file is empty")
    if raw.startswith("/") or (len(raw) > 1 and raw[1] == ":"):
        raise CitationError(
            f"rootCause.file must be sandbox-relative, not absolute: {cited_file!r}"
        )
    if ".." in PurePosixPath(raw).parts:
        raise CitationError(
            f"rootCause.file must not contain '..' path segments: {cited_file!r}"
        )

    candidate = root / raw
    canon_candidate = canonicalize(str(candidate))
    if not _is_contained(canon_candidate, canon_root):
        raise CitationError(
            f"rootCause.file escapes the sandbox: {cited_file!r} canonicalizes to "
            f"{canon_candidate!r}, sandbox root is {canon_root!r}"
        )

    real_path = Path(canon_candidate)
    if not real_path.is_file():
        raise CitationError(
            f"rootCause.file does not exist in the sandbox: {cited_file!r} "
            f"(resolved {canon_candidate!r})"
        )

    line_count = len(real_path.read_text(encoding="utf-8", errors="replace").splitlines())
    if not (1 <= cited_line <= line_count):
        raise CitationError(
            f"rootCause.line {cited_line} is out of range for {cited_file!r} - "
            f"file has {line_count} line(s)"
        )


# ── 3. check_forbidden_escalation(): the stop-law reflex, mechanized ───────────────


def check_forbidden_escalation(diagnosis: dict[str, Any]) -> dict[str, Any]:
    """
    Slice G5's own instructions: "wouldNeedForbiddenChange: true -> the incident
    ESCALATES to Hemanth instead of proceeding (ruling - the stop-law reflex,
    mechanized)." Pure: no I/O, just the one already-validated boolean flag. Returns
    {"decision": ESCALATE|PROCEED, "reason": str} - the orchestrator (diagnose(), below)
    calls this and short-circuits the confidence gate entirely when it escalates: a
    diagnosis that would need a forbidden-path change never gets to ask "is my confidence
    high enough" at all, because the answer to THAT question no longer matters.
    """
    if diagnosis["wouldNeedForbiddenChange"]:
        return {
            "decision": ESCALATE,
            "reason": (
                "diagnosis.wouldNeedForbiddenChange is true - the proposed repair would "
                "need to touch a forbidden path; escalating to Hemanth instead of "
                "proceeding to Repair (the stop-law reflex, mechanized)"
            ),
        }
    return {
        "decision": PROCEED,
        "reason": "wouldNeedForbiddenChange is false - no forbidden-path escalation needed",
    }


# ── 4. may_proceed_to_repair(): the confidence gate ─────────────────────────────────


def may_proceed_to_repair(confidence: str, min_confidence: str) -> bool:
    """
    policy.minConfidenceToRepair (default "medium") vs the diagnosis's own confidence,
    low < medium < high ordering. Pure: both arguments are plain enum strings (from an
    already-validated diagnosis and from policy.py's own CONFIDENCE_LEVELS-enforced
    minConfidenceToRepair) - this function only compares their rank. A confidence below
    the floor returns False; the orchestrator records that and does NOT proceed to
    Repair (the regression path this slice's instructions call for), it never raises for
    an ordinary "not enough confidence yet" result - only a genuinely unrecognized enum
    value raises, and even then as DiagnosisSchemaError (a schema violation, not a
    confidence-gate outcome).
    """
    if confidence not in CONFIDENCE_ORDER:
        raise DiagnosisSchemaError(
            f"confidence must be one of {sorted(CONFIDENCE_ORDER)}, got {confidence!r}"
        )
    if min_confidence not in CONFIDENCE_ORDER:
        raise DiagnosisSchemaError(
            f"min_confidence must be one of {sorted(CONFIDENCE_ORDER)}, got {min_confidence!r}"
        )
    return CONFIDENCE_ORDER[confidence] >= CONFIDENCE_ORDER[min_confidence]


# ── 5. diagnose(): the orchestration entry (live half DEFERRED) ────────────────────


def default_invoke(
    incident: dict[str, Any], sandbox_root: Path | str, *, model: str
) -> dict[str, Any]:
    """
    DEFERRED (Guardian Loop batched-runtime pass, explicitly named by this slice's own
    instructions - the ONE live diagnosis run this slice owes and does not perform): the
    real live Diagnosis invocation. Its eventual implementation would run headless
    `claude -p` with:

      - `--model <model>` where `model` is `policy.modelRouting.diagnosis` (= "opus"
        today, decision D3: "Diagnosis = Opus, hard reasoning") - never hard-coded here,
        always the value diagnose() below reads from the loaded Policy and passes through
      - `--allowedTools "Read,Grep,Glob"` - read-only tools ONLY. No Bash (Diagnosis never
        runs a command), no WebFetch/WebSearch (D3: "No web/network tools for any stage in
        v0"), no Edit/Write/MultiEdit/NotebookEdit (this slice's own "Behavior to
        preserve": "sandbox is read-only in this stage - drift tripwire on the SANDBOX too,
        no edits before Repair")
      - `--add-dir` scoped to exactly three roots: `sandbox_root` (the disposable clone at
        the incident's baseSha), the incident directory
        (`artifacts/autorepair/<incident['id']>/` - G3's own evidence packet: incident.json,
        failure.log, journey.json, warnings.json, environment.json, reproduce.ps1, grabs/),
        and `docs/encyclopedia/` (AGENTS.md's own house law - "read the right guide first
        roughly halves a cold agent's search time and wrong turns" - mechanized into the
        model's own context here so a headless Diagnosis run cannot skip it)
      - cwd pinned inside `sandbox_root`, the PreToolUse guard hook wired
        (`scripts/autorepair/hooks/guard.py --sandbox-root <sandbox_root>`, G4's own probe
        proved this mechanism live) so an escape attempt is refused and detected even if
        the prompt or `--add-dir` scoping were somehow wrong
      - a prompt assembling the incident packet's own evidence (incident.json's
        observed/expected/failingStep, the triage verdict, failure.log, journey.json) into
        one instruction: read docs/encyclopedia/ FIRST, then read the sandbox source at the
        cited seam, then answer in EXACTLY the diagnosis.json shape validate_diagnosis()
        enforces - the model is told the schema, not left to guess it

    NOT implemented and NOT called by anything in this module's deterministic tests -
    calling this raises loudly rather than silently fabricating a diagnosis, so the
    deferred boundary can never be crossed by accident. diagnose()'s own default `invoke`
    parameter points here; every deterministic test supplies its OWN canned invoke
    callable instead (mirrors triage.py's default_run_once()/run_once seam exactly).
    """
    raise NotImplementedError(
        "default_invoke() is the DEFERRED live headless-Opus diagnosis run (Guardian Loop "
        "batched-runtime pass) - pass an injected invoke callable (a canned diagnosis "
        f"dict) for deterministic use; incident={incident.get('id')!r}, "
        f"sandbox_root={sandbox_root!r}, model={model!r}"
    )


def diagnose(
    incident: dict[str, Any],
    sandbox_root: Path | str,
    *,
    policy_obj: Policy | None = None,
    invoke: Callable[..., dict[str, Any]] = default_invoke,
) -> dict[str, Any]:
    """
    Slice G5's orchestration entry: calls `invoke` (the injectable, DEFERRED live-Opus
    seam - see default_invoke()'s docstring for the exact real invocation it stands in
    for) to get a raw diagnosis, then applies every mechanical gate in order:

      1. validate_diagnosis(raw)        - closed-schema (DiagnosisSchemaError on any
                                           violation; raised BEFORE any citation or
                                           escalation logic ever sees the payload)
      2. check_citations(diagnosis, ...) - the F0-contract citation check
                                           (CitationError if rootCause.file:line doesn't
                                           resolve inside sandbox_root)
      3. check_forbidden_escalation(...) - wouldNeedForbiddenChange: true -> ESCALATE,
                                           SHORT-CIRCUITS the confidence gate entirely
                                           (the stop-law reflex takes precedence over
                                           confidence - an escalating diagnosis never
                                           gets a mayProceedToRepair=True, regardless of
                                           its stated confidence)
      4. may_proceed_to_repair(...)      - only when NOT escalating: policy.
                                           minConfidenceToRepair vs diagnosis.confidence

    `policy_obj` defaults to the real committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - exactly triage()'s own `policy_obj` parameter shape;
    this only READS JSON, it never touches a sandbox, so it is safe to call from this
    module's own tests too.

    Returns the validated diagnosis dict plus three orchestration fields:
    `incidentId`, `escalate` (bool), `escalateReason` (str), `mayProceedToRepair` (bool).
    """
    if policy_obj is None:
        policy_obj = load_policy()

    model = policy_obj.policy["modelRouting"]["diagnosis"]
    raw = invoke(incident, sandbox_root, model=model)
    diagnosis = validate_diagnosis(raw)
    check_citations(diagnosis, sandbox_root)

    escalation = check_forbidden_escalation(diagnosis)
    escalate = escalation["decision"] == ESCALATE
    if escalate:
        may_proceed = False
    else:
        min_confidence = policy_obj.policy["minConfidenceToRepair"]
        may_proceed = may_proceed_to_repair(diagnosis["confidence"], min_confidence)

    return {
        **diagnosis,
        "incidentId": incident.get("id"),
        "escalate": escalate,
        "escalateReason": escalation["reason"],
        "mayProceedToRepair": may_proceed,
    }


# ── CLI (manual sanity check on canned data only) ───────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    del argv
    canned = {
        "observed": "readerReady never fires; journey_open_manga times out waiting for it",
        "expected": "readerReady fires once the page render signal completes",
        "rootCause": {
            "file": "qml/reader/ComicReaderShell.qml",
            "line": 42,
            "claim": "readerReady is bound to reader visibility instead of the page-render "
            "signal, so it can fire before content is actually painted, or never fire "
            "at all if visibility toggles before render completes",
        },
        "seam": "ComicReaderShell.qml readerReady binding",
        "confidence": "high",
        "proposedRepair": "bind readerReady to the page-render signal instead of visibility",
        "wouldNeedForbiddenChange": False,
    }
    diagnosis = validate_diagnosis(canned)
    print("DIAGNOSIS (canned sanity check, no sandbox, no live invoke):")
    print(f"  confidence = {diagnosis['confidence']}")
    print(f"  rootCause  = {diagnosis['rootCause']['file']}:{diagnosis['rootCause']['line']}")
    escalation = check_forbidden_escalation(diagnosis)
    print(f"  escalation = {escalation['decision']}")
    may_proceed = may_proceed_to_repair(diagnosis["confidence"], "medium")
    print(f"  mayProceedToRepair (floor=medium) = {may_proceed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

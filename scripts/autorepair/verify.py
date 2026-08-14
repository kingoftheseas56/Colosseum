#!/usr/bin/env python3
"""
Colosseum Guardian Loop - Verify: the independent judge in a pristine second laboratory
(Slice G7).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G7 ("Verify - the
independent judge in a pristine second laboratory"). Purpose: the repairer never grades its
own work - a different mind, in a clean room, with only the incident, the patch, and the
bar to clear.

Decision D7 (verification builds a SECOND pristine sandbox from base+patch only): this
mechanically proves the patch is self-contained - nothing "works only with the repairer's
stray file." A `git apply` failure onto that pristine base is therefore an automatic
REJECT, before any build or gate ever runs.

Ruling 4 (adversarial verification on a different mind): "The Verifier never sees the
repair transcript or the diagnosis - only incident + base commit + patch + acceptance
criteria." This module's whole verifier-context construction exists to make that a
PROVABLE property, not a promise: build_verifier_context() below self-checks its own output
against a forbidden-exhibit marker list (find_forbidden_verifier_exhibits()) and refuses to
return a leaking context - the same "self-protection, mechanized" pattern policy.py already
established for its own three law files (ruling 1).

Binding amendment A5 (two added mechanical gates + a priming mitigation) - BINDING, this
module implements it verbatim:
  1. `ctest -N` inventory count in the verify sandbox must MATCH the base inventory (+/-
     the tests the patch added) - kills registration-tampering via build files.
  2. The diagnosis-cited files and patch-touched files must intersect - kills a drive-by
     "fix" aimed somewhere other than the blamed seam. NOTE: the orchestrator (the real,
     deferred invoke() implementation) is the ONLY place diagnosis.json is ever read during
     Verify, and only to compute this ONE boolean intersection gate - the Verifier agent
     itself never receives diagnosis.json's contents, only this gate's pass/fail outcome
     via gate_results (ruling 4's whole point, preserved even here).
  3. The Verifier's primary exhibit is a comment-stripped production diff (raw patch stays
     in its sandbox, never in the model's context) - reduces narrative priming ("here's why
     I fixed it this way" comments left by the repairer). Full identifier anonymization is
     DEFERRED (named, not silent - see the module's own DEFERRED section below).
  Acceptance criteria remain derived from the incident + policy ONLY, never from
  diagnosis.json - stated here as an assertable property of build_verifier_context()'s own
  acceptance-criteria section (see _acceptance_criteria() below, which reads only
  `incident` and `policy_obj.risk_classes` - it does not accept, import, or reference a
  diagnosis object anywhere in its signature or body).

This module splits into the same two-layer shape as triage.py (G4)/diagnosis.py
(G5)/repair_contract.py (G6):

  1. FIVE pure, hermetic pieces - zero I/O, zero subprocess, zero sandbox:
       aggregate_gates(gate_results)                    - the gate-aggregation math
       apply_failed_is_reject(apply_result)              - D7's automatic-reject rule
       build_verifier_context(incident, patch, ...)      - the ruling-4/A5 context builder
       find_forbidden_verifier_exhibits(context)          - ruling 4's provable-exclusion walk
       validate_verdict(obj)                              - the closed-schema verdict.json gate
     Every deterministic test in tests/test_autorepair_verify.py exercises these five
     directly on canned data - no sandbox, no `git apply`, no model call anywhere.

  2. run_verify(incident, patch, base_sha, sandbox_root, ...) - the orchestration entry
     that WOULD build the second pristine sandbox, `git apply` the patch, run the
     mechanical gates, and then invoke the headless-Opus Verifier + one GLM refutation -
     ALL of that LIVE work sits behind the single injectable `invoke` seam (mirrors
     diagnose()'s/run_repair()'s own seam exactly). `invoke`'s DEFAULT (default_invoke)
     raises NotImplementedError - see its own docstring for the exact real invocation
     sequence it stands in for. This is the live second-sandbox build, the live mechanical
     gate runs, the live Opus verify run, and the live GLM refutation on the golden
     patch - explicitly DEFERRED to the Guardian Loop's batched runtime pass (named here,
     not silent - owed by this slice per its own instructions).

     run_verify()'s own post-invoke() logic is 100% pure and deterministic: it calls
     apply_failed_is_reject() first (an apply failure short-circuits straight to REJECT,
     no gates, no verifier agent - D7's own ordering), then aggregate_gates() (ANY
     mechanical gate red short-circuits straight to REJECT, naming every failed gate -
     ruling 1's "the orchestrator owns the laws, not the model": a mechanically red gate
     is never overridden by asking a model's opinion), and only when every mechanical gate
     is green does it build the verifier context and validate the model's own verdict.json
     - the Verifier's approve/reject judgment only ever governs the softer, non-mechanical
     questions (cause-vs-symptom, adjacent-behavior risk, bug-test meaningfulness), never
     the hard mechanical bar.

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,triage,diagnosis,
repair_contract}.py). No pip dependencies. This module performs NO I/O of its own anywhere
- not even the one mechanical git subprocess call repair_contract.py's own
count_patch_lines() makes; every real action Verify needs (sandbox build, git apply, gate
runs, model calls) is entirely behind the injected `invoke` seam.

Public API:

    VerifyError, VerdictSchemaError                        # named refusals
    REJECT, PROCEED, APPROVE                                 # decision-string constants
    GATE_NAMES                                               # the 7 gates, canonical order
    FORBIDDEN_EXHIBIT_MARKERS                                # ruling-4 leak markers
    VERDICT_KEYS, RISK_ASSESSMENT_LEVELS                      # verdict.json's closed schema
    aggregate_gates(gate_results) -> dict                     # pure, hermetic
    apply_failed_is_reject(apply_result) -> dict               # pure, hermetic
    strip_diff_comments(patch_text) -> str                     # pure, hermetic (A5.3)
    find_forbidden_verifier_exhibits(context) -> list[str]     # pure, hermetic (ruling 4)
    build_verifier_context(incident, patch, verify_sandbox, gate_results,
                            *, policy_obj=None) -> dict         # pure given a Policy
    validate_verdict(obj) -> dict                               # pure, hermetic
    run_verify(incident, patch, base_sha, sandbox_root, *,
               policy_obj=None, invoke=default_invoke) -> dict   # orchestration
    default_invoke(incident, patch, base_sha, sandbox_root,
                    *, model, refutation) -> dict                 # DEFERRED, raises

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by this
slice per its own instructions):

  - The live second-pristine-sandbox build from base+patch (sandbox.create()/build() at
    `base_sha`, then `git apply` the patch on top - see default_invoke()'s own docstring).
  - The live mechanical gate RUNS in that sandbox: the bug-test red/green re-proof
    (test-adds-only export -> 2 runs, then full patch -> 2 runs, exactly A4's 2/2
    discipline reused for an INDEPENDENT re-proof), the original reproduce.ps1 run, the
    full `ctest -L unit` run, tests/warning_gate.ps1, the risk-class journey set (all
    tagged sessions), and both `ctest -N` inventory counts (base-only sandbox vs
    verify sandbox - A5 gate 1).
  - The one legitimate diagnosis.json read during Verify: computing the A5 gate-2
    intersection (diagnosis-cited files vs sandbox.extract_patch()'s touched files) -
    mechanical, orchestrator-side only, its OUTCOME (not its content) is all that ever
    reaches gate_results / the Verifier's context.
  - The live headless-Opus Verifier run (`claude -p --model opus`, read-only tools,
    context = build_verifier_context()'s own output) on the golden accepted G6 patch.
  - The live GLM `verifierRefutation` single-shot (thinking=high, advisory in v0) on the
    same golden patch.
  - Full identifier anonymization of the verifier's patch exhibit (A5.3's own explicitly
    named deferral, beyond comment-stripping) - not attempted by strip_diff_comments()
    below, which only removes narrative comments, never renames identifiers.

Usage (manual sanity check on canned data only - no sandbox, no `git apply`, no live
invoke; mirrors policy.py's/triage.py's/diagnosis.py's/repair_contract.py's own
`python scripts/autorepair/<module>.py` pattern):

    python scripts/autorepair/verify.py
"""

from __future__ import annotations

import fnmatch
import re
import sys
from pathlib import Path
from typing import Any, Callable

# scripts/autorepair/verify.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling imports (house pattern: flat scripts/autorepair/, no package __init__.py - see
# repair_contract.py's/diagnosis.py's identical sys.path setup). Program ruling 1: policy.py
# owns policy.modelRouting.verify/policy.verifierRefutation/policy.risk_classes; this module
# only reads them via load_policy(), it never hard-codes or re-derives its own copy.
# repair_contract.evaluate_red_green() is REUSED (never reimplemented) for the bug-test
# red/green gate - the exact same 2/2, vacuous-test-killing semantics A4 already proved out
# in G6, re-applied here to an INDEPENDENT re-proof in the second sandbox.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import Policy, load_policy  # noqa: E402  (after sys.path setup, by design)

import repair_contract  # noqa: E402  (evaluate_red_green() reused for the bug-test gate)

__all__ = [
    "REPO_ROOT",
    "VerifyError",
    "VerdictSchemaError",
    "REJECT",
    "PROCEED",
    "APPROVE",
    "GATE_NAMES",
    "FORBIDDEN_EXHIBIT_VALUE_MARKERS",
    "FORBIDDEN_EXHIBIT_KEY_NAMES",
    "VERDICT_KEYS",
    "RISK_ASSESSMENT_LEVELS",
    "aggregate_gates",
    "apply_failed_is_reject",
    "strip_diff_comments",
    "find_forbidden_verifier_exhibits",
    "build_verifier_context",
    "validate_verdict",
    "run_verify",
    "default_invoke",
]


class VerifyError(RuntimeError):
    """Base for every clean, named Verify-stage refusal (a malformed call, a leaking
    verifier context, or a verdict.json schema violation - NEVER an ordinary mechanical
    gate going red, which is always a normal {"pass": False, ...} entry in
    aggregate_gates()'s own return value, not an exception)."""


class VerdictSchemaError(VerifyError):
    """verdict.json failed closed-schema validation: unknown key, missing field, wrong
    type, or a bad enum value. Mirrors diagnosis.py's own DiagnosisSchemaError - same
    fail-closed style, own error type (this module's schema is verdict.json's, not
    diagnosis.json's)."""


# decision-string constants (mirrors diagnosis.py's ESCALATE/PROCEED pattern).
REJECT = "REJECT"
PROCEED = "PROCEED"
APPROVE = "APPROVE"


# ══════════════════════════════════════════════════════════════════════════
# 1. aggregate_gates() - the pure gate-aggregation math
# ══════════════════════════════════════════════════════════════════════════

GATE_NAMES = (
    "bugTestRedGreen",
    "reproduceNowGreen",
    "unitTestsFullPass",
    "warningGateClean",
    "journeysAllPass",
    "inventoryMatch",
    "diagnosisPatchIntersection",
)


def _bug_test_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    bug_test = gate_results.get("bugTestRedGreen")
    red = bug_test.get("redExitCodes") if isinstance(bug_test, dict) else None
    green = bug_test.get("greenExitCodes") if isinstance(bug_test, dict) else None
    try:
        repair_contract.evaluate_red_green(red, green)
    except (repair_contract.RepairContractError, VerifyError) as exc:
        return {"pass": False, "detail": str(exc)}
    return {
        "pass": True,
        "detail": (
            "bug test independently re-proven red-then-green in the verify sandbox "
            f"(red={red!r}, green={green!r})"
        ),
    }


def _reproduce_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    exit_code = gate_results.get("reproduceExitCode")
    if exit_code == 0:
        return {"pass": True, "detail": "the original failing reproduce.ps1 now exits 0"}
    return {
        "pass": False,
        "detail": f"the original failing reproduce.ps1 still does not exit 0: got {exit_code!r}",
    }


def _unit_tests_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    unit = gate_results.get("unitTests")
    if not isinstance(unit, dict):
        return {"pass": False, "detail": f"no unitTests evidence supplied: {unit!r}"}
    failed = unit.get("failed")
    failed_names = unit.get("failedNames") or []
    total = unit.get("total", "?")
    if failed == 0 and not failed_names:
        return {"pass": True, "detail": f"ctest -L unit: {total}/{total} pass"}
    return {
        "pass": False,
        "detail": f"ctest -L unit: {failed!r} failing target(s) of {total}: {sorted(failed_names)!r}",
    }


def _warning_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    warnings = gate_results.get("warningGate")
    if not isinstance(warnings, dict):
        return {"pass": False, "detail": f"no warningGate evidence supplied: {warnings!r}"}
    if warnings.get("verdict") == "WARNING_GATE_OK" and warnings.get("exitCode") == 0:
        return {"pass": True, "detail": "warning gate clean (WARNING_GATE_OK, exit 0)"}
    return {
        "pass": False,
        "detail": (
            f"warning gate not clean: verdict={warnings.get('verdict')!r} "
            f"exitCode={warnings.get('exitCode')!r}"
        ),
    }


def _journeys_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    journeys = gate_results.get("journeys")
    if not isinstance(journeys, list) or not journeys:
        return {"pass": False, "detail": f"no risk-class journey results supplied: {journeys!r}"}
    failing = [j.get("scenario", "<unnamed>") for j in journeys if not (isinstance(j, dict) and j.get("passed"))]
    if failing:
        return {"pass": False, "detail": f"journey(s) failed: {sorted(failing)!r}"}
    return {"pass": True, "detail": f"all {len(journeys)} risk-class journey(s) pass"}


def _inventory_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    """A5 gate 1: `ctest -N` inventory count in the verify sandbox must MATCH the base
    inventory count plus exactly the number of tests the patch itself added -
    registration-tampering guard (a patch that silently de-registers an existing ctest
    target from CMakeLists.txt, rather than fixing it, must not slip through)."""
    inventory = gate_results.get("inventory")
    if not isinstance(inventory, dict):
        return {"pass": False, "detail": f"no inventory evidence supplied: {inventory!r}"}
    base_count = inventory.get("baseCount")
    verify_count = inventory.get("verifyCount")
    added = inventory.get("patchAddedTestCount")
    if not all(isinstance(v, int) and not isinstance(v, bool) for v in (base_count, verify_count, added)):
        return {"pass": False, "detail": f"inventory evidence missing/non-int field(s): {inventory!r}"}
    if verify_count == base_count + added:
        return {
            "pass": True,
            "detail": (
                f"A5 inventory gate clear: verify ctest -N count {verify_count} = "
                f"base {base_count} + patch-added {added}"
            ),
        }
    return {
        "pass": False,
        "detail": (
            "REJECTED (A5 registration-tampering guard): verify ctest -N count "
            f"{verify_count} != base {base_count} + patch-added {added}"
        ),
    }


def _diagnosis_patch_intersection_gate(gate_results: dict[str, Any]) -> dict[str, Any]:
    """A5 gate 2: the diagnosis-cited files and the patch-touched files must intersect -
    the drive-by-fix guard (a repair that touches files unrelated to the blamed seam,
    however plausible-looking, carries no proof it addressed the ACTUAL diagnosed cause).
    Reads only the two file-path LISTS the orchestrator already computed (never
    diagnosis.json's prose content itself) - see the module docstring's own note on where
    the one legitimate diagnosis.json read happens (orchestrator-side, mechanical only)."""
    payload = gate_results.get("diagnosisPatchIntersection")
    if not isinstance(payload, dict):
        return {"pass": False, "detail": f"no diagnosisPatchIntersection evidence supplied: {payload!r}"}
    cited = set(payload.get("diagnosisCitedFiles") or [])
    touched = set(payload.get("patchTouchedFiles") or [])
    overlap = cited & touched
    if overlap:
        return {
            "pass": True,
            "detail": f"A5 drive-by-fix guard clear: overlap={sorted(overlap)!r}",
        }
    return {
        "pass": False,
        "detail": (
            "REJECTED (A5 drive-by-fix guard): diagnosis-cited files and patch-touched "
            f"files do not intersect - cited={sorted(cited)!r} touched={sorted(touched)!r}"
        ),
    }


_GATE_EVALUATORS: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {
    "bugTestRedGreen": _bug_test_gate,
    "reproduceNowGreen": _reproduce_gate,
    "unitTestsFullPass": _unit_tests_gate,
    "warningGateClean": _warning_gate,
    "journeysAllPass": _journeys_gate,
    "inventoryMatch": _inventory_gate,
    "diagnosisPatchIntersection": _diagnosis_patch_intersection_gate,
}


def aggregate_gates(gate_results: dict[str, Any]) -> dict[str, Any]:
    """
    Pure, hermetic gate-aggregation math (Slice G7's own instructions, verbatim): given a
    canned dict of mechanical-gate outcomes for the verify sandbox, return an overall
    pass/fail with a per-gate matrix. ANY gate red -> overall fail, naming which.

    Evaluates, in GATE_NAMES order:
      1. bugTestRedGreen           - re-proven red/green (reuses
                                      repair_contract.evaluate_red_green()'s own semantics,
                                      never reimplemented).
      2. reproduceNowGreen         - the ORIGINAL failing reproduce.ps1 now exits 0.
      3. unitTestsFullPass         - `ctest -L unit` full pass.
      4. warningGateClean          - the warning gate is clean.
      5. journeysAllPass           - the risk-class journey set all pass.
      6. inventoryMatch            - A5 gate 1 (registration-tampering guard).
      7. diagnosisPatchIntersection - A5 gate 2 (drive-by-fix guard).

    Missing or malformed per-gate evidence is treated as that gate FAILING CLOSED (never
    raises for an ordinary missing-evidence case) - absence of proof is not proof of
    passing, the same fail-closed ethos as every other Guardian Loop law file/gate.

    Returns {"overall": "PASS"|"FAIL", "gates": {gateName: {"pass": bool, "detail": str}},
    "failedGates": [<sorted gate names>]}. Raises VerifyError only for a genuinely
    malformed `gate_results` argument (not a dict at all).
    """
    if not isinstance(gate_results, dict):
        raise VerifyError(f"gate_results must be a dict; got {gate_results!r}")

    gates: dict[str, dict[str, Any]] = {
        name: _GATE_EVALUATORS[name](gate_results) for name in GATE_NAMES
    }
    failed_gates = sorted(name for name in GATE_NAMES if not gates[name]["pass"])
    overall = "FAIL" if failed_gates else "PASS"
    return {"overall": overall, "gates": gates, "failedGates": failed_gates}


# ══════════════════════════════════════════════════════════════════════════
# 2. apply_failed_is_reject() - D7's automatic-reject rule
# ══════════════════════════════════════════════════════════════════════════


def apply_failed_is_reject(apply_result: dict[str, Any]) -> dict[str, Any]:
    """
    D7: "Verification builds a SECOND pristine sandbox from base+patch only - which
    mechanically proves the patch is self-contained (nothing 'works only with the
    repairer's stray file')." A `git apply` FAILURE onto that pristine base is therefore
    an AUTOMATIC REJECT, before any build or gate ever runs - the patch is not
    self-contained, full stop.

    Pure: no I/O, no subprocess. `apply_result` is the caller's own already-collected
    {"exitCode": int, "stderr": str} outcome of a real `git apply` invocation (the live
    half; this function only judges the outcome). `stderr` is optional (defaults to "").

    Returns {"decision": REJECT|PROCEED, "reason": str} - mirrors diagnosis.py's own
    check_forbidden_escalation() return shape exactly.
    """
    if not isinstance(apply_result, dict) or "exitCode" not in apply_result:
        raise VerifyError(
            f"apply_result must be a dict with an 'exitCode' key; got {apply_result!r}"
        )
    exit_code = apply_result["exitCode"]
    if not isinstance(exit_code, int) or isinstance(exit_code, bool):
        raise VerifyError(f"apply_result['exitCode'] must be an int; got {exit_code!r}")

    if exit_code != 0:
        stderr = apply_result.get("stderr", "")
        return {
            "decision": REJECT,
            "reason": (
                "REJECTED (D7 - the second sandbox proves the patch is self-contained): "
                f"'git apply' failed with exit code {exit_code} onto the pristine base - "
                f"the patch depends on something outside itself. stderr: {stderr}"
            ),
        }
    return {
        "decision": PROCEED,
        "reason": "'git apply' succeeded cleanly onto the pristine base - the patch is self-contained",
    }


# ══════════════════════════════════════════════════════════════════════════
# 3. strip_diff_comments() - A5.3's priming-mitigation exhibit transform
# ══════════════════════════════════════════════════════════════════════════

_DIFF_META_PREFIXES = (
    "diff --git ",
    "index ",
    "--- ",
    "+++ ",
    "@@ ",
    "new file mode",
    "deleted file mode",
    "similarity index",
    "rename from",
    "rename to",
    "old mode",
    "new mode",
    "Binary files",
)
_LINE_COMMENT_RE = re.compile(r"(//|#).*$")
_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/")


def strip_diff_comments(patch_text: str) -> str:
    """
    A5.3: "the Verifier agent's primary exhibit is a comment-stripped production diff
    (raw patch remains in its sandbox) - reduces narrative priming." Best-effort,
    DELIBERATELY simple line-oriented heuristic - NOT a real tokenizer for any of the
    languages this repo ships (C++/QML/JS/Python/CMake). It strips `//...` and `#...`
    trailing line comments and same-line `/*...*/` block comments from added/removed/
    context diff BODY lines only (lines starting with '+', '-', or ' ' inside a hunk);
    diff METADATA lines (`diff --git`, `index`, `---`, `+++`, `@@`, mode/rename lines) are
    left untouched so the diff itself stays structurally parseable.

    Honest limitation (A5's own "full identifier anonymization is DEFERRED" note applies
    doubly here): a `#`/`//` inside a string literal or URL (e.g. a fragment anchor) is
    still stripped by this heuristic - acceptable because this stripped text is ONLY a
    narrative-reduction EXHIBIT shown to the Verifier model, never the patch that is
    actually `git apply`'d (that stays the untouched raw diff, entirely in the sandbox).
    """
    out_lines: list[str] = []
    for line in patch_text.splitlines():
        if not line or line[0] not in "+- " or any(line.startswith(p) for p in _DIFF_META_PREFIXES):
            out_lines.append(line)
            continue
        prefix, body = line[0], line[1:]
        body = _BLOCK_COMMENT_RE.sub("", body)
        body = _LINE_COMMENT_RE.sub("", body)
        out_lines.append(prefix + body.rstrip())
    stripped = "\n".join(out_lines)
    if patch_text.endswith("\n"):
        stripped += "\n"
    return stripped


# ══════════════════════════════════════════════════════════════════════════
# 4. find_forbidden_verifier_exhibits() / build_verifier_context() - ruling 4
# ══════════════════════════════════════════════════════════════════════════

# Ruling 4's provable-exclusion marker set, split into two deliberately different-shaped
# checks so the A5 gate-2 fields (`diagnosisPatchIntersection`, `diagnosisCitedFiles` -
# this module's OWN legitimate field names, holding plain production file-path strings,
# never diagnosis.json's own path or content) are never mistaken for a leak just because
# they share the English word "diagnosis":
#
#   FORBIDDEN_EXHIBIT_VALUE_MARKERS - substring-matched against string VALUES only. Each
#   entry unambiguously names a FILE (diagnosis.json itself; a transcript file; the
#   "attempt-" directory prefix D8 uses for each Repair attempt's evidence dir under
#   artifacts/autorepair/<id>/ - repair_contract.py's own docstring: "the sandbox's own
#   git history is the attempt ledger"). A plain file path like
#   "qml/reader/ComicReaderShell.qml" never matches any of these.
#
#   FORBIDDEN_EXHIBIT_KEY_NAMES - EXACT-matched (case-insensitive) against dict KEYS only,
#   never a substring test - so a key like "diagnosisPatchIntersection" (this module's own
#   A5 gate-2 field, an outcome BOOLEAN's supporting data, not diagnosis.json's content)
#   does not collide with the narrower, exact "diagnosis" key that WOULD signal a raw
#   diagnosis object had leaked in.
FORBIDDEN_EXHIBIT_VALUE_MARKERS = (
    "diagnosis.json",
    "attempt-",
    "repair-transcript",
    "transcript.json",
    "transcript.jsonl",
)
FORBIDDEN_EXHIBIT_KEY_NAMES = (
    "diagnosis",
    "diagnosispath",
    "diagnosisjson",
    "transcript",
    "transcriptpath",
    "repairtranscript",
)


def find_forbidden_verifier_exhibits(context: Any) -> list[str]:
    """
    Ruling 4's provable-exclusion check. Pure: no I/O, no filesystem - a structural walk
    over an ALREADY-BUILT Python object (dict/list/tuple/str, any nesting), never a real
    file. Returns a list of findings (empty = clean), each naming the offending path
    within `context` and which marker matched - a dict KEY exactly matching
    FORBIDDEN_EXHIBIT_KEY_NAMES, or a string VALUE containing a
    FORBIDDEN_EXHIBIT_VALUE_MARKERS substring. Exposed as a standalone, directly-testable
    function (per this slice's own instructions: "Expose the constructed context so a
    test can assert no diagnosis/transcript path appears in it") - a test can feed this a
    deliberately poisoned fixture to prove the check actually catches a real violation,
    independent of whether build_verifier_context()'s own real construction path happens
    to be clean.
    """
    findings: list[str] = []

    def _walk(node: Any, path: str) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                key_lower = str(key).lower()
                if key_lower in FORBIDDEN_EXHIBIT_KEY_NAMES:
                    findings.append(f"{path}.{key!s}: forbidden marker in KEY name ({key!r})")
                _walk(value, f"{path}.{key!s}")
        elif isinstance(node, (list, tuple)):
            for index, value in enumerate(node):
                _walk(value, f"{path}[{index}]")
        elif isinstance(node, str):
            lowered = node.lower()
            for marker in FORBIDDEN_EXHIBIT_VALUE_MARKERS:
                if marker in lowered:
                    findings.append(
                        f"{path}: forbidden marker in VALUE ({marker!r}) - {node[:160]!r}"
                    )

    _walk(context, "context")
    return findings


def _match_risk_class(incident: dict[str, Any], policy_obj: Policy) -> dict[str, Any]:
    """Best-effort risk-class match: the FIRST policy.risk_classes entry whose areaPattern
    (a plain fnmatch glob, not policy.py's own '**'-aware matcher - risk-classes.json's
    shipped areaPattern values are single-segment globs like '*') matches
    incident.get('scenario', '') - falling back to the LAST class (today's shipped
    risk-classes.json carries exactly one 'default' class with areaPattern '*', which
    trivially matches every scenario including an empty/missing one)."""
    scenario = incident.get("scenario") or ""
    for risk_class in policy_obj.risk_classes:
        if fnmatch.fnmatch(scenario, risk_class["areaPattern"]):
            return risk_class
    return policy_obj.risk_classes[-1]


def _acceptance_criteria(incident: dict[str, Any], policy_obj: Policy) -> dict[str, Any]:
    """Acceptance criteria, derived from `incident` (only its 'scenario' field, to match a
    risk class) and `policy_obj` (risk_classes + the two A5 gate names) ONLY - this
    function's own signature never accepts a diagnosis object, and its body never reads
    one. This IS the assertable property the plan calls for: "Acceptance criteria in the
    context derive from incident + policy ONLY, never from diagnosis.json"."""
    risk_class = _match_risk_class(incident, policy_obj)
    return {
        "riskClassId": risk_class["id"],
        "bugTestMustReproveRedThenGreen": True,
        "originalReproduceMustExitZero": True,
        "unitTestLabel": risk_class["verify"]["unitTestLabel"],
        "journeys": list(risk_class["verify"]["journeys"]),
        "warningGate": risk_class["verify"]["warningGate"],
        "inventoryMustMatchBasePlusPatchAddedTests": True,
        "citedFilesMustIntersectPatchTouchedFiles": True,
    }


def build_verifier_context(
    incident: dict[str, Any],
    patch: str,
    verify_sandbox: Path | str,
    gate_results: dict[str, Any],
    *,
    policy_obj: Policy | None = None,
) -> dict[str, Any]:
    """
    The Verifier-agent's input builder (Slice G7's own instructions + ruling 4 + A5,
    verbatim): MUST include the incident, the patch (as a comment-stripped production
    diff - A5 priming mitigation; the raw patch is kept OUT of this primary exhibit), the
    verify-sandbox path, and the gate results - and must PROVABLY EXCLUDE diagnosis.json
    and any repair-transcript path (ruling 4: the Verifier is a different mind that never
    sees how the repair was reasoned).

    `policy_obj` defaults to the REAL committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - exactly diagnose()'s/run_repair()'s own `policy_obj`
    parameter shape; only used here to derive acceptanceCriteria (risk_classes), never to
    read anything diagnosis-shaped.

    Returns {"incident": ..., "patchExhibit": <comment-stripped diff>,
    "verifySandboxPath": str, "gateResults": ..., "acceptanceCriteria": ...}.

    Self-checks its own output before returning: find_forbidden_verifier_exhibits() must
    return an empty list, or this function raises VerifyError rather than returning a
    leaking context - the same "self-protection, mechanized" pattern policy.py's own
    _assert_self_protection() established for ruling 1, applied here to ruling 4.
    """
    if policy_obj is None:
        policy_obj = load_policy()

    context = {
        "incident": incident,
        "patchExhibit": strip_diff_comments(patch),
        "verifySandboxPath": str(verify_sandbox),
        "gateResults": gate_results,
        "acceptanceCriteria": _acceptance_criteria(incident, policy_obj),
    }

    leaks = find_forbidden_verifier_exhibits(context)
    if leaks:
        raise VerifyError(
            "REFUSED (ruling 4 - the Verifier never sees the repair transcript or the "
            "diagnosis): the constructed verifier context leaked forbidden exhibit(s): "
            f"{leaks!r}"
        )
    return context


# ══════════════════════════════════════════════════════════════════════════
# 5. validate_verdict() - the closed-schema gate for verdict.json
# ══════════════════════════════════════════════════════════════════════════

VERDICT_KEYS = {"approve", "reasons", "riskAssessment"}
# Interpretation call (the plan's own text pins verdict.json's shape to
# {approve: bool, reasons: [str], riskAssessment: str} but does not enumerate
# riskAssessment's own values - flagged in the execution report): this module treats
# riskAssessment as a closed low/medium/high enum, mirroring diagnosis.py's own
# CONFIDENCE_LEVELS convention (the same three-rung vocabulary already used elsewhere in
# this program for a model's own self-assessed judgment), rather than leaving it an
# unconstrained free-form string - fail-closed is this program's whole ethos, and an
# unconstrained "riskAssessment" string could never be schema-refused for a typo/garbage
# value the way every OTHER model-output field in this program already is.
RISK_ASSESSMENT_LEVELS = {"low", "medium", "high"}


def _require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise VerdictSchemaError(f"{context} must be an object")
    return value


def _require_bool(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise VerdictSchemaError(f"{context} must be a boolean")
    return value


def _require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise VerdictSchemaError(f"{context} must be a non-empty string")
    return value


def _require_enum(value: Any, allowed: set[str], context: str) -> str:
    text = _require_string(value, context)
    if text not in allowed:
        raise VerdictSchemaError(
            f"{context} must be one of {', '.join(sorted(allowed))}; got {text!r}"
        )
    return text


def _require_string_list(value: Any, context: str, *, allow_empty: bool = True) -> list[str]:
    if not isinstance(value, list):
        raise VerdictSchemaError(f"{context} must be an array")
    result = [_require_string(item, f"{context}[{index}]") for index, item in enumerate(value)]
    if not allow_empty and not result:
        raise VerdictSchemaError(f"{context} must not be empty")
    return result


def _require_exact_keys(obj: dict[str, Any], allowed: set[str], context: str) -> None:
    unknown = set(obj) - allowed
    if unknown:
        raise VerdictSchemaError(
            f"{context} contains unknown field(s): " + ", ".join(sorted(unknown))
        )
    missing = allowed - set(obj)
    if missing:
        raise VerdictSchemaError(
            f"{context} missing required field(s): " + ", ".join(sorted(missing))
        )


def validate_verdict(obj: Any) -> dict[str, Any]:
    """
    verdict.json's closed-schema gate (Slice G7's own output contract, verbatim:
    `{approve: bool, reasons: [str], riskAssessment: str}`), fail-closed like
    diagnosis.py's validate_diagnosis(): unknown key -> refuse; missing field -> refuse;
    wrong type -> refuse; bad riskAssessment enum value -> refuse. Pure: no I/O.

    Interpretation call (flagged again here, at the point it bites): `reasons` is
    required to be a NON-EMPTY list of non-empty strings - a verdict (approve OR reject)
    with zero stated reasons carries no judgment a human or a retry loop could act on;
    the plan's own text does not explicitly pin emptiness either way.

    Returns a fresh, normalized dict on success; raises VerdictSchemaError naming the
    exact offending field on any violation.
    """
    verdict = _require_object(obj, "verdict")
    _require_exact_keys(verdict, VERDICT_KEYS, "verdict")

    approve = _require_bool(verdict["approve"], "verdict.approve")
    reasons = _require_string_list(verdict["reasons"], "verdict.reasons", allow_empty=False)
    risk_assessment = _require_enum(
        verdict["riskAssessment"], RISK_ASSESSMENT_LEVELS, "verdict.riskAssessment"
    )

    return {"approve": approve, "reasons": reasons, "riskAssessment": risk_assessment}


# ══════════════════════════════════════════════════════════════════════════
# run_verify(): the orchestration entry (live half DEFERRED)
# ══════════════════════════════════════════════════════════════════════════


def default_invoke(
    incident: dict[str, Any],
    patch: str,
    base_sha: str,
    sandbox_root: Path | str,
    *,
    model: str,
    refutation: dict[str, Any],
) -> dict[str, Any]:
    """
    DEFERRED (Guardian Loop batched-runtime pass, explicitly named by this slice's own
    instructions - the live second-sandbox build, mechanical gate runs, Opus verify run,
    and GLM refutation this slice owes and does not perform): the real live Verify
    sequence. Its eventual implementation would:

      1. sandbox.create(base_sha, incident['id'] + '-verify', ...) + sandbox.build(clone)
         (Slice G2, reused not reimplemented) - the SECOND pristine sandbox, from
         `base_sha` alone, never touching the repairer's own first sandbox (D7's whole
         point: this sandbox's only inputs are base_sha + the patch text applied fresh).
      2. `git apply <patch>` inside that clone, in the MAIN repo's working tree sense (the
         sandbox IS the working tree) - capture {"exitCode": int, "stderr": str} as
         `applyResult`. A nonzero exit here means apply_failed_is_reject() below rejects
         immediately; nothing past this point ever runs.
      3. On a clean apply: run the mechanical gates INDEPENDENTLY (never trusting G6's own
         report) -
           - bug-test red/green: export test-adds-only -> run the repair's declared
             bugtest command 2x (RED expected), then apply production changes too -> run
             it 2x more (GREEN expected) - A4's own 2/2 discipline, reused for an
             independent re-proof, not a re-read of G6's numbers.
           - the ORIGINAL incident's reproduce.ps1, run once more - expect exit 0 now.
           - `ctest -L unit --output-on-failure` in full.
           - tests/warning_gate.ps1 over this sandbox's own session logs.
           - the risk-class journey set (policy.risk_classes matched to the incident's
             area) - every one, as tagged `lanista session run` invocations.
           - `ctest -N` in this verify sandbox AND in a separate base-only (no-patch)
             pristine sandbox - the two counts feed A5 gate 1 (`inventory`).
         Assembles all of the above into one `gateResults` dict matching
         aggregate_gates()'s own expected shape.
      4. The ONE legitimate diagnosis.json read of the whole Verify stage (orchestrator-
         side only, never handed to the model): read the incident's diagnosis.json's
         rootCause.file (and any other cited paths), intersect against
         sandbox.extract_patch()'s own touched-path lists from the FIRST (repair) sandbox
         - feeds A5 gate 2 (`diagnosisPatchIntersection`) into the same `gateResults` dict.
      5. build_verifier_context(incident, patch, clone, gateResults, policy_obj=...) - the
         comment-stripped, diagnosis/transcript-free exhibit this function's caller
         (run_verify()) will hand the model.
      6. Headless `claude -p --model <model>` where `model` is policy.modelRouting.verify
         (= "opus" today, D3: "Verifier = Opus - different seat/context than the
         repairer") - never hard-coded here, always the value run_verify() below reads
         from the loaded Policy and passes through. `--allowedTools "Read,Grep,Glob"`
         (read-only, mirrors diagnosis.py's own tool set - the Verifier never edits
         anything). `--add-dir` scoped to ONLY the verify sandbox + the incident dir MINUS
         diagnosis.json and any attempt-*/ subpath (never the whole incident dir
         unfiltered - ruling 4 must hold even against a curious Read/Glob call the prompt
         didn't ask for). Prompt = build_verifier_context()'s own JSON, asking for
         `verdict.json` in EXACTLY validate_verdict()'s shape - the model is told the
         schema, not left to guess it (mirrors diagnosis.py's own default_invoke()).
      7. ONE GLM single-shot refutation call, per `refutation` (=
         policy.verifierRefutation: provider="glm", thinking="high", advisory=True today)
         - summarizes the patch and asks GLM to argue AGAINST approval; recorded in the
         dossier as `refutation`, does not itself veto while `advisory` is true (D3: "one
         GLM high-thinking single-shot refutation, advisory in v0").

    Returns (in the real implementation) {"applyResult": {...}, "gateResults": {...},
    "verifierRaw": {...} | None, "refutation": {...} | None} - `verifierRaw`/`refutation`
    are None whenever apply or the mechanical gates already rejected (steps 6-7 never ran
    in that case; run_verify() below never inspects them unless it got past
    aggregate_gates() clean).

    NOT implemented and NOT called by anything in this module's deterministic tests -
    calling this raises loudly rather than silently fabricating an apply result, gate
    results, or a verdict, so the deferred boundary can never be crossed by accident.
    run_verify()'s own default `invoke` parameter points here; every deterministic test
    supplies its OWN canned invoke callable instead (mirrors triage.py's
    default_run_once()/diagnosis.py's/repair_contract.py's own default_invoke() seam
    exactly).
    """
    raise NotImplementedError(
        "default_invoke() is the DEFERRED live Verify sequence (Guardian Loop batched "
        "runtime pass) - pass an injected invoke callable (canned applyResult/"
        "gateResults/verifierRaw/refutation) for deterministic use; "
        f"incident={incident.get('id')!r}, base_sha={base_sha!r}, "
        f"sandbox_root={sandbox_root!r}, model={model!r}, refutation={refutation!r}"
    )


def run_verify(
    incident: dict[str, Any],
    patch: str,
    base_sha: str,
    sandbox_root: Path | str,
    *,
    policy_obj: Policy | None = None,
    invoke: Callable[..., dict[str, Any]] = default_invoke,
) -> dict[str, Any]:
    """
    Slice G7's orchestration entry: calls `invoke` ONCE (the injectable, DEFERRED live
    seam covering the entire second-sandbox build, `git apply`, mechanical gate runs,
    Opus verify run, and GLM refutation - see default_invoke()'s own docstring for the
    exact real sequence it stands in for), then applies every mechanical/pure gate in
    strict order, short-circuiting to REJECT the moment any hard gate fails:

      1. apply_failed_is_reject(live['applyResult'])  - D7. A failed `git apply` rejects
                                                          immediately; nothing else runs.
      2. aggregate_gates(live['gateResults'])          - ruling 1/A5. ANY mechanical gate
                                                          red rejects immediately, naming
                                                          every failed gate - a model's
                                                          opinion is never asked for or
                                                          needed to override a red
                                                          mechanical gate.
      3. build_verifier_context(...) + validate_verdict(live['verifierRaw']) - only
         reached once every mechanical gate is green. The Verifier's own approve/reject
         judgment governs ONLY from here on (ruling 4: "it is allowed and expected to
         reject" even with every mechanical gate green - cause-vs-symptom, adjacent-
         behavior risk, bug-test meaningfulness are the model's job, not a script's).

    `policy_obj` defaults to the REAL committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - exactly diagnose()'s/run_repair()'s own `policy_obj`
    parameter shape; this only reads JSON (via load_policy()) plus whatever `invoke`
    itself does, so calling this with a canned `invoke` never touches a sandbox.

    Returns one of three shapes, all sharing {"incidentId", "stage", "decision", "approve",
    "reasons", "riskAssessment", "gates", "refutation"}:
      stage="apply"    - D7 auto-reject; gates/riskAssessment/refutation are None.
      stage="gates"     - a mechanical gate rejected; riskAssessment/refutation are None;
                           `gates` carries the full aggregate_gates() matrix.
      stage="verifier"  - every mechanical gate passed; the Verifier's own verdict decides
                           `decision`/`approve`/`reasons`/`riskAssessment`; `refutation`
                           carries the GLM refutation payload (advisory, never itself
                           overrides `decision`); `verifierContext` is also included (the
                           exact context the model was shown - useful for the promotion
                           dossier and for a test to assert the ruling-4 property held on
                           the REAL orchestration path, not just build_verifier_context()
                           called standalone).

    `decision` is REJECT|APPROVE - REJECT sends the incident back to the G6 retry loop
    (Slice G7's own instructions: "Reject -> G6 retry loop with reasons"); APPROVE hands
    off to G8 (Promotion).
    """
    if policy_obj is None:
        policy_obj = load_policy()

    model = policy_obj.policy["modelRouting"]["verify"]
    refutation_policy = policy_obj.policy["verifierRefutation"]

    live = invoke(incident, patch, base_sha, sandbox_root, model=model, refutation=refutation_policy)

    apply_decision = apply_failed_is_reject(live["applyResult"])
    if apply_decision["decision"] == REJECT:
        return {
            "incidentId": incident.get("id"),
            "stage": "apply",
            "decision": REJECT,
            "approve": False,
            "reasons": [apply_decision["reason"]],
            "riskAssessment": None,
            "gates": None,
            "refutation": None,
        }

    gate_results = live["gateResults"]
    gate_agg = aggregate_gates(gate_results)
    if gate_agg["overall"] == "FAIL":
        reasons = [f"{name}: {gate_agg['gates'][name]['detail']}" for name in gate_agg["failedGates"]]
        return {
            "incidentId": incident.get("id"),
            "stage": "gates",
            "decision": REJECT,
            "approve": False,
            "reasons": reasons,
            "riskAssessment": None,
            "gates": gate_agg,
            "refutation": None,
        }

    context = build_verifier_context(incident, patch, sandbox_root, gate_results, policy_obj=policy_obj)
    verdict = validate_verdict(live["verifierRaw"])
    decision = APPROVE if verdict["approve"] else REJECT

    return {
        "incidentId": incident.get("id"),
        "stage": "verifier",
        "decision": decision,
        "approve": verdict["approve"],
        "reasons": verdict["reasons"],
        "riskAssessment": verdict["riskAssessment"],
        "gates": gate_agg,
        "refutation": live.get("refutation"),
        "verifierContext": context,
    }


# ── CLI (manual sanity check on canned data only) ───────────────────────────


def main(argv: list[str] | None = None) -> int:
    del argv

    all_green = {
        "bugTestRedGreen": {"redExitCodes": [1, 1], "greenExitCodes": [0, 0]},
        "reproduceExitCode": 0,
        "unitTests": {"label": "unit", "total": 43, "failed": 0, "failedNames": []},
        "warningGate": {"verdict": "WARNING_GATE_OK", "exitCode": 0},
        "journeys": [{"scenario": "tests/lanista_scenarios/journey_open_manga.json", "passed": True}],
        "inventory": {"baseCount": 43, "verifyCount": 44, "patchAddedTestCount": 1},
        "diagnosisPatchIntersection": {
            "diagnosisCitedFiles": ["qml/reader/ComicReaderShell.qml"],
            "patchTouchedFiles": ["qml/reader/ComicReaderShell.qml"],
        },
    }
    agg = aggregate_gates(all_green)
    print("GATE AGGREGATION (canned sanity check, all green):")
    print(f"  overall = {agg['overall']}")
    print(f"  failedGates = {agg['failedGates']}")

    apply_ok = apply_failed_is_reject({"exitCode": 0})
    apply_bad = apply_failed_is_reject({"exitCode": 1, "stderr": "patch does not apply"})
    print(f"APPLY (exit 0): {apply_ok['decision']}")
    print(f"APPLY (exit 1): {apply_bad['decision']}")

    print("run_verify()'s live half is DEFERRED - see default_invoke()'s own docstring.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

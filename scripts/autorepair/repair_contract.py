#!/usr/bin/env python3
"""
Colosseum Guardian Loop - Repair: handcuffed edits, mandatory bug test, mechanical
red/green (Slice G6).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G6 ("Repair -
handcuffed edits, mandatory bug test, mechanical red/green"). Purpose: produce a patch
that fixes the cause and carries its own proof, under handcuffs that make thermometer-
tampering mechanically impossible. This module is the PURE, unit-testable repair
CONTRACT - no agent, no build, no subprocess beyond a mechanical `git diff --numstat`
line count. It enforces, in code:

  Program ruling 2 ("the patient never rewrites the thermometer") - a repair patch may
  ADD test files; it may not MODIFY or DELETE any existing file under tests/, the
  forbidden-paths list, or the Guardian Loop's own machinery. Enforced mechanically by
  diff classification, never by agent goodwill.

  Program ruling 5 ("the bug must be proven to have existed") - the orchestrator splits
  every patch into test-additions and production-changes: test-only applied -> bug test
  MUST fail (red); production applied -> MUST pass (green). A repair whose test cannot go
  red is rejected - the house negative-control law, mechanized.

  D6 (patch mechanics) - classification into {testAdds, forbidden, production} is
  scripts/autorepair/sandbox.py's extract_patch() OWN job (Program ruling 1: "the
  orchestrator owns the laws, not the model" - policy.py's Policy.is_forbidden() is the
  law, sandbox.py already calls it to build these three buckets). This module NEVER
  reimplements that classification; it only VALIDATES the shape of an already-classified
  result (validate_patch_shape()) and constructs `patch_line_count` via a mechanical git
  count the orchestrator computes itself (count_patch_lines()) - self-reported line counts
  from the repair agent are never trusted, per ruling 1.

  Amendment A4 (bug-test command is template-constrained) - the declared bug-test command
  must be EXACTLY `ctest -R <name>` or `lanista session run <scenario>`, and the name/
  scenario must correspond to a path the patch itself ADDED (present in testAdds) - so the
  agent cannot point its proof at a fixture it controls elsewhere. Red and green are each
  proven on EXACTLY 2 runs (kills timing-lucky proofs); a red exit code of 0 on any run is
  the exact vacuous-bug-test fake proof this program exists to kill.

  Amendment A8 (maxPatchLines) - policy.maxPatchLines (default 400) caps patch size;
  oversized patches escalate to Hemanth instead of promoting.

This module splits into the same two-layer shape as triage.py (G4) and diagnosis.py (G5):

  1. Three PURE, hermetic gates - zero I/O, zero subprocess, zero sandbox:
       validate_patch_shape(classification, *, max_patch_lines, patch_line_count)
       validate_bugtest_command(bugtest, testAdds)
       evaluate_red_green(red_exit_codes, green_exit_codes)
     Every deterministic test in tests/test_autorepair_contract.py exercises only these
     three, on canned classifications/bugtest declarations/exit-code lists.

  2. count_patch_lines(clone) - the ONE piece of real (but still non-agent, non-build)
     I/O this module performs: a mechanical `git diff --cached --numstat` line count,
     computed by the orchestrator itself so patch size can never be gamed by a self-report
     from the repair agent (ruling 1).

  3. run_repair(incident, diagnosis, sandbox_root, ...) - the orchestration entry that
     WOULD run the headless Sonnet repair agent (attempt loop <= policy.maxRepairAttempts,
     each retry fed the prior rejection VERBATIM), reuse sandbox.extract_patch() (never
     reimplement classification), run count_patch_lines(), then apply gates 1-3 in order.
     ALL the LIVE parts - the Sonnet agent call itself, and the real scratch-build
     red/green runs it must produce exit codes from - sit behind the single injectable
     `invoke` seam, exactly triage.py's run_once / diagnosis.py's invoke pattern.
     `invoke`'s DEFAULT (default_invoke) raises NotImplementedError - see its own
     docstring for the exact real invocation it stands in for. This is the live Sonnet
     repair run on the golden incident, explicitly DEFERRED to the Guardian Loop's batched
     runtime pass (named here, not silent - owed by this slice per its own instructions).

Ownership split at the invoke() seam (an interpretation call this slice's own text leaves
open - flagged in the execution report, not silent): `invoke()` is responsible for every
LIVE, impure action a single repair attempt needs - running the headless Sonnet agent
inside the sandbox, resetting the sandbox working tree to the incident's base SHA before
making its edits (so each attempt's diff, as extract_patch() sees it via `git diff
--cached` against HEAD, is that attempt's OWN full candidate patch, never a stack of prior
attempts), recording a commit onto the sandbox's own attempt-ledger history ("Behavior to
preserve": "the sandbox's own git history is the attempt ledger"), and driving the real
pristine-scratch-export red/green builds to produce exit codes. run_repair() itself only
ever does MECHANICAL, deterministic work: call invoke(), then reuse sandbox.extract_patch()
+ count_patch_lines() + this module's own three gates, in order, never re-deriving any of
invoke()'s live behavior itself.

Stdlib only (house pattern - scripts/autorepair/{policy,sandbox,triage,diagnosis}.py). No
pip dependencies. This module's own I/O is limited to count_patch_lines()'s single git
subprocess call; every gate function is pure.

Public API:

    RepairContractError                                    # the one named refusal type
    validate_patch_shape(classification, *, max_patch_lines, patch_line_count) -> None
    validate_bugtest_command(bugtest, testAdds) -> None
    evaluate_red_green(red_exit_codes, green_exit_codes) -> None
    count_patch_lines(clone) -> int
    run_repair(incident, diagnosis, sandbox_root, *, policy_obj=None, invoke=default_invoke) -> dict
    default_invoke(incident, diagnosis, sandbox_root, *, attempt, prior_rejection, model) -> dict

DEFERRED to the Guardian Loop's batched runtime pass (named here, not silent - owed by
this slice per its own instructions):

  - The live headless-Sonnet repair run on the golden incident (G5's diagnosis in hand),
    attempt loop against a REAL sandbox, real pristine-scratch-export red/green builds -
    see default_invoke()'s own docstring for the exact real invocation it stands in for.
  - The budget-exhaustion regression path exercised on an impossible canned LIVE task
    (this slice's own text: "attempts=0 policy override... clean escalation report, no
    patch") - this module's deterministic tests exercise the SAME control-flow path with a
    canned invoke() instead (see RunRepairOrchestrationSeamTests.
    test_budget_exhaustion_after_all_attempts_reject), so the logic is proven; only the
    LIVE run against a real impossible task is deferred.

Usage (manual sanity check on canned data only - no sandbox, no live run; mirrors
policy.py's/triage.py's/diagnosis.py's own `python scripts/autorepair/<module>.py`
pattern):

    python scripts/autorepair/repair_contract.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Callable

# scripts/autorepair/repair_contract.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling imports (house pattern: flat scripts/autorepair/, no package __init__.py - see
# triage.py's/diagnosis.py's identical sys.path setup). Program ruling 1: policy.py owns
# policy.maxRepairAttempts/maxPatchLines/modelRouting.repair; this module only reads them
# via load_policy(), it never hard-codes or re-derives its own copy. sandbox.py's
# extract_patch() is REUSED (never reimplemented) for D6 classification - this module
# calls it, it does not re-derive Policy.is_forbidden()'s rules itself.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import Policy, load_policy  # noqa: E402  (after sys.path setup, by design)

# sandbox is imported for extract_patch() (called for real by run_repair()) AND for type/
# seam documentation in default_invoke()'s docstring - unlike triage.py's deferred-only
# import, run_repair() DOES call sandbox.extract_patch() for real (it is pure git
# plumbing, no build, no agent - exactly the kind of "real but mechanical" I/O this
# module's own docstring above draws the line at).
import sandbox  # noqa: E402

__all__ = [
    "REPO_ROOT",
    "RepairContractError",
    "BUGTEST_KEYS",
    "BUGTEST_ALLOWED_CMDS",
    "RED_GREEN_RUNS_REQUIRED",
    "validate_patch_shape",
    "validate_bugtest_command",
    "evaluate_red_green",
    "count_patch_lines",
    "run_repair",
    "default_invoke",
]

# ── the A4 command template ─────────────────────────────────────────────────

BUGTEST_KEYS = {"cmd", "args", "expectRedWithoutFix"}
BUGTEST_ALLOWED_CMDS = {"ctest", "lanista"}

# A4: "red and green are each proven 2/2 runs (kills timing-lucky proofs)."
RED_GREEN_RUNS_REQUIRED = 2


class RepairContractError(RuntimeError):
    """The one named G6 repair-contract refusal: a rejected patch shape (ruling 2/5, A8),
    a rejected bug-test command (A4, template-constrained), or a rejected red/green proof
    (ruling 5/A4, mechanical). Every reject path in this module raises exactly this type -
    the orchestrator (run_repair(), below) catches it, feeds its message verbatim into the
    next attempt's invoke() call, and never needs to distinguish subtypes to do so."""


# ══════════════════════════════════════════════════════════════════════════
# 1. validate_patch_shape() - ruling 2 (forbidden), ruling 5 (bug-test door), A8 (size)
# ══════════════════════════════════════════════════════════════════════════


def validate_patch_shape(
    classification: dict[str, list[str]],
    *,
    max_patch_lines: int,
    patch_line_count: int,
) -> None:
    """
    Pure, hermetic. `classification` MUST be sandbox.extract_patch()'s own return shape -
    {"testAdds": [...], "forbidden": [...], "production": [...]} - this function never
    calls extract_patch() itself and never re-derives Policy.is_forbidden()'s rules; it
    only judges the ALREADY-CLASSIFIED result's shape. `patch_line_count` is likewise
    supplied by the caller (count_patch_lines(), below, in the real orchestration path) -
    this function performs no I/O to compute it itself, so a test can hand it any int.

    REJECTS (raises RepairContractError), checked in this order:
      1. `classification["forbidden"]` non-empty (ruling 2 - the patch touched a
         protected path: an existing test, the Guardian Loop's own machinery, or another
         forbidden-paths.json entry).
      2. `classification["testAdds"]` empty (ruling 5 - a repair MUST add a bug test; this
         is "the bug-test door" - a patch with zero new tests under tests/ carries no
         proof the bug ever existed).
      3. `classification["production"]` empty (F3 hardening, Guardian Loop audit - a
         repair that adds only a test and changes no production code proves nothing was
         actually fixed; a real repair changes production code, not test files alone).
      4. `patch_line_count > max_patch_lines` (A8 - an oversized patch escalates to
         Hemanth instead of promoting).

    ACCEPTS (returns None) only when all three checks clear - a patch that added at least
    one test, touched zero forbidden paths, and stayed within the size budget.
    """
    if not isinstance(classification, dict) or not {"testAdds", "forbidden", "production"} <= set(
        classification
    ):
        raise RepairContractError(
            "classification must be sandbox.extract_patch()'s own "
            "{testAdds, forbidden, production} dict; got "
            f"{classification!r}"
        )

    forbidden = classification["forbidden"]
    test_adds = classification["testAdds"]

    if forbidden:
        raise RepairContractError(
            "REJECTED (Program ruling 2 - the patient never rewrites the thermometer): "
            f"the patch touches forbidden path(s): {sorted(forbidden)!r}"
        )

    if not test_adds:
        raise RepairContractError(
            "REJECTED (Program ruling 5 - the bug must be proven to have existed): the "
            "patch adds no test under tests/ - a repair MUST add a bug test that proves "
            "the bug existed (the bug-test door; testAdds is empty)"
        )

    if not classification["production"]:
        raise RepairContractError(
            "REJECTED (F3 - a repair must change production code, not only add tests): "
            "the patch's classification.production is empty - a bug test proves the bug "
            "existed, but only a production-code change can prove it was actually fixed"
        )

    if patch_line_count > max_patch_lines:
        raise RepairContractError(
            "REJECTED (amendment A8 - oversized patches escalate instead of promoting): "
            f"patch_line_count={patch_line_count} exceeds policy.maxPatchLines="
            f"{max_patch_lines}"
        )


# ══════════════════════════════════════════════════════════════════════════
# 2. validate_bugtest_command() - A4, template-constrained
# ══════════════════════════════════════════════════════════════════════════


def _canon_path(raw: str) -> str:
    """Minimal forward-slash normalization for a testAdds-style path comparison - mirrors
    sandbox.py's own _canonicalize() intent without reaching into that module's private
    symbol (diagnosis.py's own stated reason for keeping small local copies rather than
    coupling to another module's un-exported internals)."""
    return raw.strip().replace("\\", "/")


def validate_bugtest_command(bugtest: dict[str, Any], testAdds: list[str]) -> None:
    """
    Pure, hermetic. Amendment A4: "the bug-test command is template-constrained, not
    free-form" - it must be EXACTLY `ctest -R <name>` or `lanista session run <scenario>`,
    and the referenced name/scenario must correspond to a path the patch itself ADDED
    (present in `testAdds`) - so the repair agent cannot point its proof at a fixture it
    controls elsewhere (a script outside tests/, a pre-existing scenario it can silently
    edit, etc).

    `bugtest` is the repair's own declared contract: {"cmd": "ctest"|"lanista", "args":
    [...], "expectRedWithoutFix": True}. `testAdds` is
    sandbox.extract_patch()['testAdds'] - the same list validate_patch_shape() already
    required to be non-empty.

    Interpretation call (this slice's own text leaves the exact correspondence rule open -
    flagged in the execution report): a `ctest -R <name>` target does not name a file path
    (ctest registers named targets, e.g. via CMake's `add_test(NAME <name> ...)`) so it
    cannot be checked for LITERAL membership in `testAdds`. This function instead requires
    `<name>` to equal the POSIX filename STEM (no directory, no extension) of at least one
    testAdds path - e.g. a patch that adds `tests/auto/foo/tst_bug12345.cpp` licenses
    exactly `ctest -R tst_bug12345`, matching this repo's own CMake convention of naming a
    test target after its source file. A `lanista session run <scenario>` target, by
    contrast, names a scenario FILE PATH directly (mirrors
    `tests/lanista_scenarios/journey_*.json`), so it is checked for literal (canonicalized)
    membership in `testAdds` instead - no stem derivation needed or applied.

    REJECTS (raises RepairContractError):
      - `bugtest` is not an object, or has an unknown/missing key (closed shape: exactly
        {cmd, args, expectRedWithoutFix}).
      - `cmd` is anything other than "ctest" or "lanista" (free-form commands like `bash
        foo.sh` are refused outright - A4's whole point).
      - the args shape is not EXACTLY `["-R", <name>]` (ctest) or `["session", "run",
        <scenario>]` (lanista).
      - the resolved target does not correspond to any path in `testAdds` (per the
        interpretation above) - the agent pointed its proof somewhere it should not have.
      - `expectRedWithoutFix` is missing, not a bool, or `False` - A4 requires the repair
        to explicitly declare its own bug test as one that is EXPECTED to fail without the
        fix; a declaration that does not even claim this is not a proof.

    ACCEPTS (returns None) only when the command matches one of the two templates exactly
    and its target is among the patch's own testAdds.
    """
    if not isinstance(bugtest, dict):
        raise RepairContractError(f"bugtest must be an object; got {bugtest!r}")

    unknown = set(bugtest) - BUGTEST_KEYS
    if unknown:
        raise RepairContractError(
            f"bugtest contains unknown field(s): {', '.join(sorted(unknown))}"
        )
    missing = BUGTEST_KEYS - set(bugtest)
    if missing:
        raise RepairContractError(
            f"bugtest missing required field(s): {', '.join(sorted(missing))}"
        )

    cmd = bugtest["cmd"]
    args = bugtest["args"]
    expect_red = bugtest["expectRedWithoutFix"]

    if not isinstance(cmd, str) or not cmd:
        raise RepairContractError(f"bugtest.cmd must be a non-empty string; got {cmd!r}")
    if not isinstance(args, list) or not all(isinstance(a, str) for a in args):
        raise RepairContractError(f"bugtest.args must be a list of strings; got {args!r}")
    # bool is a subclass of int, not of str, so an accidental int/None here is already
    # caught by isinstance(expect_red, bool) below - no separate bool-vs-int guard needed
    # (unlike policy.py's require_positive_int, which guards the opposite direction).
    if not isinstance(expect_red, bool):
        raise RepairContractError(
            f"bugtest.expectRedWithoutFix must be a boolean; got {expect_red!r}"
        )
    if not expect_red:
        raise RepairContractError(
            "bugtest.expectRedWithoutFix must be true (A4) - a bug test that does not "
            "even declare itself expected to fail without the fix is not a proof"
        )

    if cmd not in BUGTEST_ALLOWED_CMDS:
        raise RepairContractError(
            "bugtest.cmd must be 'ctest' or 'lanista' (A4, template-constrained, never "
            f"free-form); got {cmd!r}"
        )

    if cmd == "ctest":
        if not (len(args) == 2 and args[0] == "-R" and args[1]):
            raise RepairContractError(
                "bugtest command must be exactly 'ctest -R <name>' (A4); got "
                f"cmd={cmd!r} args={args!r}"
            )
        name = args[1]
        test_add_stems = {PurePosixPath(_canon_path(p)).stem for p in testAdds}
        if name not in test_add_stems:
            raise RepairContractError(
                f"ctest -R target {name!r} does not correspond to any path the patch "
                f"added under tests/ (testAdds={sorted(testAdds)!r}) - A4: the agent "
                "cannot point its proof at a fixture it controls elsewhere"
            )
        return

    # cmd == "lanista" (the only other member of BUGTEST_ALLOWED_CMDS).
    if not (len(args) == 3 and args[0] == "session" and args[1] == "run" and args[2]):
        raise RepairContractError(
            "bugtest command must be exactly 'lanista session run <scenario>' (A4); got "
            f"cmd={cmd!r} args={args!r}"
        )
    scenario = _canon_path(args[2])
    canon_test_adds = {_canon_path(p) for p in testAdds}
    if scenario not in canon_test_adds:
        raise RepairContractError(
            f"lanista scenario target {args[2]!r} does not correspond to any path the "
            f"patch added under tests/ (testAdds={sorted(testAdds)!r}) - A4: the agent "
            "cannot point its proof at a fixture it controls elsewhere"
        )


# ══════════════════════════════════════════════════════════════════════════
# 3. evaluate_red_green() - ruling 5 / A4, mechanical, kills the vacuous bug test
# ══════════════════════════════════════════════════════════════════════════


def evaluate_red_green(red_exit_codes: list[int], green_exit_codes: list[int]) -> None:
    """
    Pure, hermetic. Amendment A4/D6/Program ruling 5, mechanical: `red_exit_codes` are the
    bug test's exit codes with test-additions ONLY applied (production NOT applied) -
    every one MUST be nonzero (the test genuinely fails without the fix), and there must
    be EXACTLY 2 (A4: "red proven 2/2 runs" - kills a timing-lucky single proof).
    `green_exit_codes` are with production ALSO applied - every one MUST be zero (the fix
    makes it pass), also EXACTLY 2.

    REJECTS (raises RepairContractError):
      - either list has other than exactly 2 entries (covers "fewer than 2" and any
        over-count alike - A4 pins the shape to 2/2 exactly, not "at least 2").
      - any red exit code is 0 - THE vacuous bug test: a test that PASSES even without the
        production fix proves nothing about the bug ever existing. Named explicitly in the
        raised message as the exact class of fake proof this program exists to kill.
      - any green exit code is nonzero - the production fix does not actually make the
        declared bug test pass.

    ACCEPTS (returns None) only when both lists have exactly 2 entries, every red code is
    nonzero, and every green code is exactly 0.
    """
    if not isinstance(red_exit_codes, list) or not isinstance(green_exit_codes, list):
        raise RepairContractError(
            "red_exit_codes and green_exit_codes must both be lists of int exit codes; "
            f"got red={red_exit_codes!r} green={green_exit_codes!r}"
        )

    if len(red_exit_codes) != RED_GREEN_RUNS_REQUIRED:
        raise RepairContractError(
            f"REJECTED (A4 - red must be proven on exactly {RED_GREEN_RUNS_REQUIRED} "
            f"runs): got {len(red_exit_codes)} red run(s): {red_exit_codes!r}"
        )
    if len(green_exit_codes) != RED_GREEN_RUNS_REQUIRED:
        raise RepairContractError(
            f"REJECTED (A4 - green must be proven on exactly {RED_GREEN_RUNS_REQUIRED} "
            f"runs): got {len(green_exit_codes)} green run(s): {green_exit_codes!r}"
        )

    vacuous = [i for i, code in enumerate(red_exit_codes) if code == 0]
    if vacuous:
        raise RepairContractError(
            "REJECTED - VACUOUS BUG TEST (Program ruling 5 / A4): the bug test exited 0 "
            f"(passed) on red run index(es) {vacuous} WITHOUT the production fix applied "
            f"(test-adds-only build) - red_exit_codes={red_exit_codes!r}. A red run that "
            "exits 0 never actually proved the bug existed - this is the exact class of "
            "fake proof this program exists to kill."
        )

    failed_green = [i for i, code in enumerate(green_exit_codes) if code != 0]
    if failed_green:
        raise RepairContractError(
            "REJECTED - GREEN DID NOT PASS (Program ruling 5 / A4): the bug test did not "
            f"exit 0 on green run index(es) {failed_green} WITH the production fix "
            f"applied - green_exit_codes={green_exit_codes!r}. The declared fix does not "
            "actually make its own bug test pass."
        )


# ══════════════════════════════════════════════════════════════════════════
# count_patch_lines() - the one real (mechanical, non-agent, non-build) I/O
# ══════════════════════════════════════════════════════════════════════════


def count_patch_lines(clone: Path | str) -> int:
    """
    A8's `patch_line_count` input, computed MECHANICALLY by the orchestrator itself via
    `git diff --cached --numstat` in the sandbox clone - never trusted as a self-report
    from the repair agent (Program ruling 1: "the orchestrator owns the laws, not the
    model"). Requires the sandbox's index to already be staged exactly as
    sandbox.extract_patch() leaves it (that function's own `git add -A` call) - run_repair()
    below calls extract_patch() first, then this, against the identical staged index,
    never re-staging in between, so both functions see the SAME patch.

    Sums the insertions + deletions column from `git diff --cached --numstat` across every
    changed path. A binary file reports "-" for both columns (git's own numstat
    convention) - counted as 1 line each so it still counts toward the size budget rather
    than silently vanishing from it, without pretending to know its real diff size.
    """
    clone_path = Path(clone).resolve()
    result = subprocess.run(
        ["git", "diff", "--cached", "--numstat"],
        cwd=str(clone_path),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RepairContractError(
            f"count_patch_lines(): 'git diff --cached --numstat' failed in {clone_path}: "
            f"{result.stderr}"
        )

    total = 0
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        added_raw, removed_raw = parts[0], parts[1]
        added = 1 if added_raw == "-" else int(added_raw)
        removed = 1 if removed_raw == "-" else int(removed_raw)
        total += added + removed
    return total


# ══════════════════════════════════════════════════════════════════════════
# run_repair(): the orchestration entry (live half DEFERRED)
# ══════════════════════════════════════════════════════════════════════════


def default_invoke(
    incident: dict[str, Any],
    diagnosis: dict[str, Any],
    sandbox_root: Path | str,
    *,
    attempt: int,
    prior_rejection: str | None,
    model: str,
) -> dict[str, Any]:
    """
    DEFERRED (Guardian Loop batched-runtime pass, explicitly named by this slice's own
    instructions - the live Sonnet repair run this slice owes and does not perform): the
    real live Repair invocation for ONE attempt. Its eventual implementation would:

      1. Reset the sandbox working tree to the incident's base SHA (so this attempt's diff
         is its OWN full candidate patch, never a stack of prior rejected attempts).
      2. Run headless `claude -p --model <model>` where `model` is
         `policy.modelRouting.repair` (= "sonnet" today, decision D3: "Repair = Sonnet,
         mechanical, diagnosis in hand") - never hard-coded here, always the value
         run_repair() below reads from the loaded Policy and passes through.
      3. `--allowedTools "Read,Grep,Glob,Edit,Write,Bash"` with Bash running UNDER the
         PreToolUse guard hook (G4's own proven mechanism) - Bash is needed here (unlike
         Diagnosis's read-only tool set) because the repair agent must be able to run
         builds and its own test inside the sandbox.
      4. cwd pinned inside `sandbox_root`, `--add-dir` scoped to the sandbox + the incident
         directory + `docs/encyclopedia/` (same house-law scoping as diagnosis.py's own
         default_invoke()).
      5. A prompt assembling: the incident packet (observed/expected/failingStep), the
         diagnosis (rootCause, seam, proposedRepair), and the contract text VERBATIM -
         "you must ADD a test that fails without your fix; you may not modify existing
         tests; forbidden paths: <policy.forbidden.modifyDelete>" - plus, when
         `prior_rejection` is not None, that exact rejection message appended so a retry
         sees precisely why its last attempt was refused (the plan's own "each retry
         receives the prior verifier/contract rejection verbatim").
      6. After the agent's edits: `git -C sandbox add -A && git commit` (the attempt-ledger
         commit - "Behavior to preserve": "the sandbox's own git history is the attempt
         ledger, commit per attempt").
      7. The mechanical red/green proof itself: a PRISTINE SCRATCH EXPORT of the sandbox
         with ONLY the patch's testAdds applied (production changes excluded) -> run the
         agent's declared bugtest command TWICE, collect both exit codes as
         `redExitCodes`; then the SAME export with production changes ALSO applied ->
         incremental rebuild -> run the same bugtest command TWICE more, collect both exit
         codes as `greenExitCodes`. `QML_DISABLE_DISK_CACHE=1` is set in the environment
         for every one of these four red/green sessions (A4 belt-and-braces - tagged
         sessions already isolate CacheLocation, this additionally defeats any QML disk
         cache from masking a genuine red/green difference between runs).

    Returns (in the real implementation) {"bugtest": {...}, "redExitCodes": [c1, c2],
    "greenExitCodes": [c1, c2]} - the raw payload run_repair() below feeds through
    sandbox.extract_patch() + count_patch_lines() + this module's own three gates.

    NOT implemented and NOT called by anything in this module's deterministic tests -
    calling this raises loudly rather than silently fabricating a patch or exit codes, so
    the deferred boundary can never be crossed by accident. run_repair()'s own default
    `invoke` parameter points here; every deterministic test supplies its OWN canned
    invoke callable instead (mirrors triage.py's default_run_once()/diagnosis.py's
    default_invoke() seam exactly).
    """
    raise NotImplementedError(
        "default_invoke() is the DEFERRED live headless-Sonnet repair run (Guardian Loop "
        "batched-runtime pass) - pass an injected invoke callable (canned bugtest + "
        f"red/green exit codes) for deterministic use; incident={incident.get('id')!r}, "
        f"sandbox_root={sandbox_root!r}, attempt={attempt}, "
        f"prior_rejection={prior_rejection!r}, model={model!r}"
    )


def run_repair(
    incident: dict[str, Any],
    diagnosis: dict[str, Any],
    sandbox_root: Path | str,
    *,
    policy_obj: Policy | None = None,
    invoke: Callable[..., dict[str, Any]] = default_invoke,
) -> dict[str, Any]:
    """
    Slice G6's orchestration entry: an attempt loop <= `policy.maxRepairAttempts`, each
    attempt calling `invoke` (the injectable, DEFERRED live-Sonnet seam - see
    default_invoke()'s docstring for the exact real invocation it stands in for, and the
    module docstring's "ownership split" note for what invoke() vs run_repair() itself is
    responsible for), then applying every mechanical gate in order:

      1. sandbox.extract_patch(sandbox_root, policy_obj) - D6 classification, REUSED
         (never reimplemented) from Slice G2.
      2. count_patch_lines(sandbox_root)                  - A8's mechanical line count,
                                                              computed here, never trusted
                                                              from invoke()'s own payload.
      3. validate_patch_shape(classification, ...)         - ruling 2 / ruling 5 / A8.
      4. validate_bugtest_command(raw["bugtest"], ...)      - A4, template-constrained.
      5. evaluate_red_green(raw["redExitCodes"], raw["greenExitCodes"]) - ruling 5 / A4.

    On any RepairContractError from steps 3-5, the attempt is recorded as rejected and,
    if attempts remain, the NEXT invoke() call receives that exact exception message
    (`str(exc)`) as its `prior_rejection` keyword argument - VERBATIM, per the plan's own
    "each retry receives the prior verifier/contract rejection verbatim." When all
    `policy.maxRepairAttempts` attempts are exhausted without an accepted patch (or when
    `policy.maxRepairAttempts` is 0 - the plan's own budget-exhaustion regression-path
    case - "attempts=0 policy override... clean escalation report, no patch"), invoke() is
    never called again and this function returns a clean `accepted: False` escalation
    report instead of raising.

    `policy_obj` defaults to the REAL committed docs/autorepair/ law (load_policy() with
    no arguments) if not supplied - exactly triage()'s/diagnose()'s own `policy_obj`
    parameter shape; this only reads JSON plus (via count_patch_lines()) one mechanical
    git subprocess call, so it never itself builds or runs an agent.

    Returns, on acceptance: {"accepted": True, "incidentId", "attempts" (the 1-based
    attempt number that succeeded), "attemptLog" (one entry per attempt, rejected or
    accepted), "classification", "bugtest", "redExitCodes", "greenExitCodes"}.

    Returns, on exhaustion: {"accepted": False, "incidentId", "attempts" (=
    policy.maxRepairAttempts), "attemptLog", "escalateReason"}.
    """
    if policy_obj is None:
        policy_obj = load_policy()

    model = policy_obj.policy["modelRouting"]["repair"]
    max_attempts = policy_obj.policy["maxRepairAttempts"]
    max_patch_lines = policy_obj.policy["maxPatchLines"]

    prior_rejection: str | None = None
    attempt_log: list[dict[str, Any]] = []

    for attempt in range(1, max_attempts + 1):
        raw = invoke(
            incident,
            diagnosis,
            sandbox_root,
            attempt=attempt,
            prior_rejection=prior_rejection,
            model=model,
        )

        try:
            classification = sandbox.extract_patch(sandbox_root, policy_obj)
            patch_line_count = count_patch_lines(sandbox_root)

            validate_patch_shape(
                classification,
                max_patch_lines=max_patch_lines,
                patch_line_count=patch_line_count,
            )
            bugtest = raw["bugtest"]
            validate_bugtest_command(bugtest, classification["testAdds"])
            evaluate_red_green(raw["redExitCodes"], raw["greenExitCodes"])
        except RepairContractError as exc:
            prior_rejection = str(exc)
            attempt_log.append({"attempt": attempt, "accepted": False, "reason": prior_rejection})
            continue

        attempt_log.append({"attempt": attempt, "accepted": True})
        return {
            "accepted": True,
            "incidentId": incident.get("id"),
            "attempts": attempt,
            "attemptLog": attempt_log,
            "classification": classification,
            "patchLineCount": patch_line_count,
            "bugtest": bugtest,
            "redExitCodes": raw["redExitCodes"],
            "greenExitCodes": raw["greenExitCodes"],
        }

    if max_attempts == 0:
        escalate_reason = (
            "policy.maxRepairAttempts is 0 - no repair attempts permitted; escalating "
            "without ever calling invoke()"
        )
    else:
        escalate_reason = (
            f"exhausted policy.maxRepairAttempts={max_attempts} without an accepted "
            f"patch; last rejection: {prior_rejection}"
        )

    return {
        "accepted": False,
        "incidentId": incident.get("id"),
        "attempts": max_attempts,
        "attemptLog": attempt_log,
        "escalateReason": escalate_reason,
    }


# ── CLI (manual sanity check on canned data only) ───────────────────────────


def main(argv: list[str] | None = None) -> int:
    del argv

    clean_classification = {
        "testAdds": ["tests/test_bug_12345.py"],
        "forbidden": [],
        "production": ["native/engine/Foo.cpp"],
    }
    validate_patch_shape(clean_classification, max_patch_lines=400, patch_line_count=42)
    print("PATCH SHAPE (canned sanity check): ACCEPTED")

    bugtest = {"cmd": "ctest", "args": ["-R", "test_bug_12345"], "expectRedWithoutFix": True}
    validate_bugtest_command(bugtest, clean_classification["testAdds"])
    print("BUGTEST COMMAND (canned sanity check): ACCEPTED")

    evaluate_red_green([1, 1], [0, 0])
    print("RED/GREEN (canned sanity check, red=[1,1] green=[0,0]): ACCEPTED")

    try:
        evaluate_red_green([0, 0], [0, 0])
    except RepairContractError as exc:
        print(f"RED/GREEN (canned vacuous-bugtest check, red=[0,0]): REJECTED - {exc}")

    print("run_repair()'s live half is DEFERRED - see default_invoke()'s own docstring.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

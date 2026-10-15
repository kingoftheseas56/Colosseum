#!/usr/bin/env python3
"""test_autorepair_verify.py - tests for scripts/autorepair/verify.py (Guardian Loop
Slice G7: "Verify - the independent judge in a pristine second laboratory").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G7, decision D7,
ruling 4, and binding amendment A5. Pure Python, stdlib unittest, house flat convention
(D8: tests/test_autorepair_*.py). Runnable directly:

    python tests/test_autorepair_verify.py -v

HERMETIC end to end: no sandbox is built, no `git apply` runs for real, no `claude -p`
session runs, no GLM call is made anywhere in this file. Every gate/context/verdict
function is exercised on hand-built canned dicts. The orchestration tests inject a canned
`invoke` callable; the one real deferred boundary (default_invoke()) is asserted to raise
NotImplementedError, never called for real.

Test groups:
  GateAggregationTests               - all-green canned gates -> overall pass; each single
                                        gate red (all 7 gates, one at a time) -> overall
                                        fail naming exactly that gate, including the 5
                                        explicitly mandated cases (unit-test red, warning
                                        red, reproduce-still-red, A5 inventory-count
                                        mismatch, A5 empty diagnosis-patch intersection)
                                        plus bug-test red and journey red for completeness.
  GateAggregationNegativeControlTests - THE MANDATORY NEGATIVE CONTROL, both directions,
                                        at the pure aggregate_gates() level: bug test green
                                        + exactly ONE -L unit target red -> overall REJECT
                                        naming the failing target; that same target fixed
                                        to green -> overall PASS, with every other gate
                                        held constant.
  ApplyFailureTests                   - D7: a nonzero git-apply exit code -> REJECT naming
                                        D7 and the exit code; exit 0 -> PROCEED.
  DiffCommentStrippingTests            - A5.3: a narrative `//`/`#` comment on an
                                        added/removed line is stripped; diff metadata
                                        lines (`diff --git`, `---`, `+++`, `@@`) are left
                                        byte-identical; code content survives.
  VerifierContextTests                 - build_verifier_context() contains incident/patch
                                        exhibit/sandbox path/gate results/acceptance
                                        criteria; find_forbidden_verifier_exhibits() is
                                        exercised BOTH directions - a clean context returns
                                        empty, a deliberately poisoned fixture (a
                                        diagnosis.json path, an attempt-N/ transcript path)
                                        returns non-empty naming the leak;
                                        build_verifier_context() itself refuses (raises) if
                                        ever handed a patch/incident that would leak;
                                        acceptance criteria reference no "diagnosis"
                                        anywhere, derived from incident+policy only.
  VerdictSchemaTests                   - a valid verdict loads; unknown key, missing key,
                                        wrong-type approve, wrong-type reasons, empty
                                        reasons, and a bad riskAssessment enum each refuse,
                                        naming the offender; a corrected riskAssessment is
                                        accepted (negative control, both directions).
  RunVerifyOrchestrationSeamTests      - run_verify()'s injectable invoke seam, canned end
                                        to end: a failed git-apply short-circuits to
                                        stage="apply"/REJECT without ever touching gates;
                                        a red mechanical gate short-circuits to
                                        stage="gates"/REJECT naming the failing target,
                                        WITHOUT ever consulting verifierRaw (the injected
                                        canned invoke's verifierRaw is poisoned/absent in
                                        that case and must never be read); the SAME
                                        scenario with that one gate fixed green reaches the
                                        Verifier stage and, given a clean approve=True
                                        verdict, returns decision=APPROVE - the exact
                                        reject-then-approve pairing this slice's negative
                                        control calls for, now proven through the full
                                        orchestration path; a clean-gates but
                                        Verifier-rejects case proves "it is allowed and
                                        expected to reject" even past every mechanical
                                        gate; model routing comes from policy (never
                                        hard-coded); default_invoke() raises
                                        NotImplementedError; run_verify()'s own default
                                        parameter reaches default_invoke().
"""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VERIFY_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "verify.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


verify_mod = _load_module("autorepair_verify", VERIFY_SCRIPT_PATH)

aggregate_gates = verify_mod.aggregate_gates
apply_failed_is_reject = verify_mod.apply_failed_is_reject
strip_diff_comments = verify_mod.strip_diff_comments
find_forbidden_verifier_exhibits = verify_mod.find_forbidden_verifier_exhibits
build_verifier_context = verify_mod.build_verifier_context
validate_verdict = verify_mod.validate_verdict
run_verify = verify_mod.run_verify
VerifyError = verify_mod.VerifyError
VerdictSchemaError = verify_mod.VerdictSchemaError
REJECT = verify_mod.REJECT
PROCEED = verify_mod.PROCEED
APPROVE = verify_mod.APPROVE


# ══════════════════════════════════════════════════════════════════════════
# canned fixtures
# ══════════════════════════════════════════════════════════════════════════


def _all_green_gate_results(**overrides) -> dict:
    payload = {
        "bugTestRedGreen": {"redExitCodes": [1, 1], "greenExitCodes": [0, 0]},
        "reproduceExitCode": 0,
        "unitTests": {"label": "unit", "total": 43, "failed": 0, "failedNames": []},
        "warningGate": {"verdict": "WARNING_GATE_OK", "exitCode": 0},
        "journeys": [
            {"scenario": "tests/lanista_scenarios/journey_open_manga.json", "passed": True},
            {"scenario": "tests/lanista_scenarios/journey_play_video.json", "passed": True},
        ],
        "inventory": {"baseCount": 43, "verifyCount": 44, "patchAddedTestCount": 1},
        "diagnosisPatchIntersection": {
            "diagnosisCitedFiles": ["qml/reader/ComicReaderShell.qml"],
            "patchTouchedFiles": ["qml/reader/ComicReaderShell.qml", "tests/tst_bug1.cpp"],
        },
    }
    payload.update(overrides)
    return payload


def _incident() -> dict:
    return {
        "id": "AR-2026-08-14-0001",
        "baseSha": "deadbeef",
        "scenario": "tests/lanista_scenarios/journey_open_manga.json",
    }


class _CannedPolicy:
    """A minimal stand-in for scripts/autorepair/policy.py's Policy dataclass - only the
    fields verify.py's own functions actually read (modelRouting.verify,
    verifierRefutation, risk_classes). Mirrors test_autorepair_diagnosis.py's/
    test_autorepair_contract.py's own _CannedPolicy pattern."""

    def __init__(self):
        self.policy = {
            "modelRouting": {"diagnosis": "opus", "repair": "sonnet", "verify": "opus"},
            "verifierRefutation": {"provider": "glm", "thinking": "high", "advisory": True},
        }
        self.risk_classes = (
            {
                "id": "default",
                "areaPattern": "*",
                "verify": {
                    "unitTestLabel": "unit",
                    "journeys": ["tests/lanista_scenarios/journey_open_manga.json"],
                    "warningGate": "tests/warning_gate.ps1",
                },
            },
        )


# ══════════════════════════════════════════════════════════════════════════
# aggregate_gates() - the pure gate-aggregation math
# ══════════════════════════════════════════════════════════════════════════


class GateAggregationTests(unittest.TestCase):
    def test_all_green_passes(self):
        result = aggregate_gates(_all_green_gate_results())
        self.assertEqual(result["overall"], "PASS")
        self.assertEqual(result["failedGates"], [])
        for name in verify_mod.GATE_NAMES:
            self.assertTrue(result["gates"][name]["pass"], f"{name} should pass")

    def test_bug_test_red_rejects_naming_it(self):
        gates = _all_green_gate_results(
            bugTestRedGreen={"redExitCodes": [0, 0], "greenExitCodes": [0, 0]}
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["bugTestRedGreen"])
        self.assertIn("VACUOUS", result["gates"]["bugTestRedGreen"]["detail"])

    def test_reproduce_still_red_rejects_naming_it(self):
        gates = _all_green_gate_results(reproduceExitCode=1)
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["reproduceNowGreen"])
        self.assertIn("does not exit 0", result["gates"]["reproduceNowGreen"]["detail"])

    def test_unit_test_red_rejects_naming_the_failing_target(self):
        gates = _all_green_gate_results(
            unitTests={"label": "unit", "total": 43, "failed": 1, "failedNames": ["tst_comick_db_url"]}
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["unitTestsFullPass"])
        self.assertIn("tst_comick_db_url", result["gates"]["unitTestsFullPass"]["detail"])

    def test_warning_gate_red_rejects_naming_it(self):
        gates = _all_green_gate_results(warningGate={"verdict": "FAIL", "exitCode": 1})
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["warningGateClean"])
        self.assertIn("not clean", result["gates"]["warningGateClean"]["detail"])

    def test_journey_red_rejects_naming_it(self):
        gates = _all_green_gate_results(
            journeys=[
                {"scenario": "tests/lanista_scenarios/journey_open_manga.json", "passed": True},
                {"scenario": "tests/lanista_scenarios/journey_play_video.json", "passed": False},
            ]
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["journeysAllPass"])
        self.assertIn("journey_play_video.json", result["gates"]["journeysAllPass"]["detail"])

    def test_a5_inventory_mismatch_rejects_naming_it(self):
        gates = _all_green_gate_results(
            inventory={"baseCount": 43, "verifyCount": 43, "patchAddedTestCount": 1}
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["inventoryMatch"])
        message = result["gates"]["inventoryMatch"]["detail"]
        self.assertIn("A5", message)
        self.assertIn("registration-tampering", message)

    def test_a5_empty_diagnosis_patch_intersection_rejects_naming_it(self):
        gates = _all_green_gate_results(
            diagnosisPatchIntersection={
                "diagnosisCitedFiles": ["qml/reader/ComicReaderShell.qml"],
                "patchTouchedFiles": ["native/engine/Unrelated.cpp"],
            }
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["diagnosisPatchIntersection"])
        message = result["gates"]["diagnosisPatchIntersection"]["detail"]
        self.assertIn("A5", message)
        self.assertIn("drive-by-fix", message)

    def test_multiple_red_gates_are_all_named(self):
        gates = _all_green_gate_results(
            reproduceExitCode=1,
            warningGate={"verdict": "FAIL", "exitCode": 1},
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["reproduceNowGreen", "warningGateClean"])

    def test_missing_gate_evidence_fails_closed_not_raises(self):
        result = aggregate_gates({})
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(sorted(result["failedGates"]), sorted(verify_mod.GATE_NAMES))

    def test_non_dict_gate_results_raises(self):
        with self.assertRaises(VerifyError):
            aggregate_gates(["not", "a", "dict"])


# ══════════════════════════════════════════════════════════════════════════
# F2 hardening (Guardian Loop audit, MEDIUM-HIGH): bool-as-int fails these gates OPEN
# ══════════════════════════════════════════════════════════════════════════


class BoolNotIntGateGuardTests(unittest.TestCase):
    """_reproduce_gate/_warning_gate/_unit_tests_gate used `== 0` comparisons where
    Python's `False == 0` is True, so a malformed `false` PASSED - contradicting
    aggregate_gates()'s own fail-closed docstring. Guarded with the same isinstance(x,
    int) and not isinstance(x, bool) check _inventory_gate() already used."""

    def test_reproduce_gate_bool_exit_code_fails_closed(self):
        result = verify_mod._reproduce_gate({"reproduceExitCode": False})
        self.assertFalse(result["pass"])

    def test_warning_gate_bool_exit_code_fails_closed(self):
        result = verify_mod._warning_gate(
            {"warningGate": {"verdict": "WARNING_GATE_OK", "exitCode": False}}
        )
        self.assertFalse(result["pass"])

    def test_unit_tests_gate_bool_failed_count_fails_closed(self):
        result = verify_mod._unit_tests_gate(
            {"unitTests": {"failed": False, "failedNames": [], "total": "?"}}
        )
        self.assertFalse(result["pass"])

    def test_genuine_int_zero_still_passes_all_three_gates(self):
        """Negative control, direction 2: a genuine int 0 (never a bool) must still
        pass - proving the guard fails closed on bool specifically, not on 0 itself."""
        self.assertTrue(verify_mod._reproduce_gate({"reproduceExitCode": 0})["pass"])
        self.assertTrue(
            verify_mod._warning_gate(
                {"warningGate": {"verdict": "WARNING_GATE_OK", "exitCode": 0}}
            )["pass"]
        )
        self.assertTrue(
            verify_mod._unit_tests_gate(
                {"unitTests": {"failed": 0, "failedNames": [], "total": 43}}
            )["pass"]
        )

    def test_aggregate_gates_end_to_end_bool_false_flips_overall_to_fail(self):
        """End to end through aggregate_gates(): a bool-false reproduceExitCode must
        flip overall to FAIL, not silently PASS the whole gate matrix - the exact
        contradiction of aggregate_gates()'s own fail-closed docstring this closes."""
        gates = _all_green_gate_results(reproduceExitCode=False)
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertIn("reproduceNowGreen", result["failedGates"])


# ══════════════════════════════════════════════════════════════════════════
# The mandatory negative control (pure aggregate_gates level, both directions)
# ══════════════════════════════════════════════════════════════════════════


class GateAggregationNegativeControlTests(unittest.TestCase):
    """The plan's mandatory negative control (Slice G7's own "Focused tests" contract):
    'canned patch that passes its bug test but reds one unit test -> overall REJECT with
    the failing target named; clean patch -> approve path.'"""

    def test_bug_test_green_one_unit_test_red_overall_rejects_naming_target(self):
        gates = _all_green_gate_results(
            unitTests={
                "label": "unit",
                "total": 43,
                "failed": 1,
                "failedNames": ["colosseum.qttest.some_unrelated_target"],
            }
        )
        # The exact assertion: the bug test itself is genuinely green (this is NOT the
        # vacuous-bug-test case) but the whole verdict still rejects, naming precisely
        # the one failing ctest target - ruling 1's "the orchestrator owns the laws":
        # one red mechanical gate outvotes an otherwise-clean bug-test proof.
        self.assertTrue(aggregate_gates(_all_green_gate_results())["gates"]["bugTestRedGreen"]["pass"])
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "FAIL")
        self.assertEqual(result["failedGates"], ["unitTestsFullPass"])
        self.assertIn("colosseum.qttest.some_unrelated_target", result["gates"]["unitTestsFullPass"]["detail"])
        self.assertTrue(result["gates"]["bugTestRedGreen"]["pass"], "the bug test gate itself stays green")

    def test_same_target_fixed_green_flips_to_overall_pass(self):
        gates = _all_green_gate_results(
            unitTests={"label": "unit", "total": 43, "failed": 0, "failedNames": []}
        )
        result = aggregate_gates(gates)
        self.assertEqual(result["overall"], "PASS")
        self.assertEqual(result["failedGates"], [])


# ══════════════════════════════════════════════════════════════════════════
# apply_failed_is_reject() - D7
# ══════════════════════════════════════════════════════════════════════════


class ApplyFailureTests(unittest.TestCase):
    def test_nonzero_exit_rejects_naming_d7_and_exit_code(self):
        result = apply_failed_is_reject({"exitCode": 1, "stderr": "patch does not apply cleanly"})
        self.assertEqual(result["decision"], REJECT)
        self.assertIn("D7", result["reason"])
        self.assertIn("1", result["reason"])
        self.assertIn("patch does not apply cleanly", result["reason"])

    def test_zero_exit_proceeds(self):
        result = apply_failed_is_reject({"exitCode": 0})
        self.assertEqual(result["decision"], PROCEED)

    def test_missing_stderr_defaults_to_empty(self):
        result = apply_failed_is_reject({"exitCode": 1})
        self.assertEqual(result["decision"], REJECT)

    def test_malformed_apply_result_raises(self):
        with self.assertRaises(VerifyError):
            apply_failed_is_reject({"notExitCode": 0})

    def test_bool_exit_code_raises(self):
        """bool is a subclass of int in Python - guard against True/False silently
        satisfying the int check (mirrors policy.py's own require_positive_int guard)."""
        with self.assertRaises(VerifyError):
            apply_failed_is_reject({"exitCode": True})


# ══════════════════════════════════════════════════════════════════════════
# strip_diff_comments() - A5.3's priming-mitigation exhibit transform
# ══════════════════════════════════════════════════════════════════════════


class DiffCommentStrippingTests(unittest.TestCase):
    def test_narrative_comment_on_added_line_is_stripped(self):
        patch = (
            "diff --git a/native/engine/Foo.cpp b/native/engine/Foo.cpp\n"
            "index 1111111..2222222 100644\n"
            "--- a/native/engine/Foo.cpp\n"
            "+++ b/native/engine/Foo.cpp\n"
            "@@ -1,1 +1,1 @@\n"
            "+bindReady(pageRenderSignal);  // the bug was binding to visibility instead\n"
        )
        stripped = strip_diff_comments(patch)
        self.assertNotIn("the bug was binding to visibility instead", stripped)
        self.assertIn("bindReady(pageRenderSignal);", stripped)

    def test_diff_metadata_lines_untouched(self):
        patch = (
            "diff --git a/native/engine/Foo.cpp b/native/engine/Foo.cpp\n"
            "index 1111111..2222222 100644\n"
            "--- a/native/engine/Foo.cpp\n"
            "+++ b/native/engine/Foo.cpp\n"
            "@@ -1,1 +1,1 @@\n"
            "+// this line starts with a comment marker but is a hunk header above it\n"
        )
        stripped = strip_diff_comments(patch)
        lines = stripped.splitlines()
        self.assertEqual(lines[0], "diff --git a/native/engine/Foo.cpp b/native/engine/Foo.cpp")
        self.assertEqual(lines[1], "index 1111111..2222222 100644")
        self.assertEqual(lines[2], "--- a/native/engine/Foo.cpp")
        self.assertEqual(lines[3], "+++ b/native/engine/Foo.cpp")
        self.assertEqual(lines[4], "@@ -1,1 +1,1 @@")

    def test_python_hash_comment_stripped(self):
        patch = "+value = 1  # narrative: this fixes the off-by-one\n"
        stripped = strip_diff_comments(patch)
        self.assertNotIn("narrative", stripped)
        self.assertIn("value = 1", stripped)

    def test_code_without_comments_is_unchanged(self):
        patch = "+int x = 1;\n-int x = 0;\n context line unchanged\n"
        self.assertEqual(strip_diff_comments(patch), patch)


# ══════════════════════════════════════════════════════════════════════════
# find_forbidden_verifier_exhibits() / build_verifier_context() - ruling 4
# ══════════════════════════════════════════════════════════════════════════


class VerifierContextTests(unittest.TestCase):
    def setUp(self) -> None:
        self.incident = _incident()
        self.patch = (
            "diff --git a/qml/reader/ComicReaderShell.qml b/qml/reader/ComicReaderShell.qml\n"
            "--- a/qml/reader/ComicReaderShell.qml\n"
            "+++ b/qml/reader/ComicReaderShell.qml\n"
            "@@ -1,1 +1,1 @@\n"
            "+readerReady: pageRenderSignal  // fixed the binding\n"
        )
        self.gate_results = _all_green_gate_results()
        self.policy = _CannedPolicy()

    def test_clean_context_contains_the_required_exhibits(self):
        context = build_verifier_context(
            self.incident, self.patch, "C:/arsbx/AR-verify", self.gate_results,
            policy_obj=self.policy,
        )
        self.assertEqual(context["incident"], self.incident)
        self.assertIn("readerReady", context["patchExhibit"])
        self.assertNotIn("fixed the binding", context["patchExhibit"])
        self.assertEqual(context["verifySandboxPath"], "C:/arsbx/AR-verify")
        self.assertEqual(context["gateResults"], self.gate_results)
        self.assertIn("acceptanceCriteria", context)

    def test_clean_context_has_zero_forbidden_exhibits(self):
        context = build_verifier_context(
            self.incident, self.patch, "C:/arsbx/AR-verify", self.gate_results,
            policy_obj=self.policy,
        )
        self.assertEqual(find_forbidden_verifier_exhibits(context), [])

    def test_poisoned_context_with_diagnosis_path_is_caught(self):
        poisoned = {
            "incident": self.incident,
            "patchExhibit": "clean",
            "diagnosisPath": "artifacts/autorepair/AR-2026-08-14-0001/diagnosis.json",
        }
        findings = find_forbidden_verifier_exhibits(poisoned)
        self.assertTrue(findings, "a diagnosis.json path must be caught")
        self.assertTrue(any("diagnosis.json" in f for f in findings))

    def test_poisoned_context_with_repair_transcript_path_is_caught(self):
        poisoned = {
            "incident": self.incident,
            "repairAttemptLog": "artifacts/autorepair/AR-2026-08-14-0001/attempt-2/transcript.jsonl",
        }
        findings = find_forbidden_verifier_exhibits(poisoned)
        self.assertTrue(findings, "an attempt-N/ transcript path must be caught")
        joined = " ".join(findings)
        self.assertIn("attempt-", joined)

    def test_poisoned_context_via_forbidden_key_name_is_caught(self):
        """Even without a suspicious VALUE, a key literally named 'diagnosis' is itself
        a leak signal - the walk checks keys, not just string values."""
        poisoned = {"diagnosis": {"rootCause": {"file": "x", "line": 1}}}
        findings = find_forbidden_verifier_exhibits(poisoned)
        self.assertTrue(findings)

    def test_build_verifier_context_refuses_when_patch_exhibit_would_leak(self):
        """If the incident dict itself somehow carried a diagnosis-shaped field (a caller
        bug upstream), build_verifier_context() must refuse rather than silently pass it
        through - the self-protection check runs on its OWN constructed output, which
        embeds the incident dict verbatim."""
        poisoned_incident = {**self.incident, "diagnosisPath": "artifacts/.../diagnosis.json"}
        with self.assertRaises(VerifyError) as ctx:
            build_verifier_context(
                poisoned_incident, self.patch, "C:/arsbx/AR-verify", self.gate_results,
                policy_obj=self.policy,
            )
        self.assertIn("ruling 4", str(ctx.exception))

    def test_acceptance_criteria_never_reference_diagnosis(self):
        context = build_verifier_context(
            self.incident, self.patch, "C:/arsbx/AR-verify", self.gate_results,
            policy_obj=self.policy,
        )
        criteria = context["acceptanceCriteria"]
        blob = repr(criteria).lower()
        self.assertNotIn("diagnosis", blob)
        # Derived from policy.risk_classes (incident's scenario matched against
        # areaPattern), never invented ad hoc.
        self.assertEqual(criteria["riskClassId"], "default")
        self.assertEqual(criteria["unitTestLabel"], "unit")
        self.assertIn("tests/lanista_scenarios/journey_open_manga.json", criteria["journeys"])

    def test_acceptance_criteria_derive_only_from_incident_and_policy(self):
        """Same incident/policy, DIFFERENT gate_results and patch -> identical acceptance
        criteria - proving the criteria never vary with gate outcomes or patch content,
        only with incident+policy, exactly as ruling 4/A5 require."""
        context_a = build_verifier_context(
            self.incident, self.patch, "C:/arsbx/AR-verify", _all_green_gate_results(),
            policy_obj=self.policy,
        )
        context_b = build_verifier_context(
            self.incident, "diff --git a/x b/x\n", "C:/arsbx/AR-verify",
            _all_green_gate_results(reproduceExitCode=1),
            policy_obj=self.policy,
        )
        self.assertEqual(context_a["acceptanceCriteria"], context_b["acceptanceCriteria"])


# ══════════════════════════════════════════════════════════════════════════
# validate_verdict() - the closed-schema gate for verdict.json
# ══════════════════════════════════════════════════════════════════════════


def _verdict(**overrides) -> dict:
    payload = {
        "approve": True,
        "reasons": ["every mechanical gate is green", "the fix addresses the cited root cause"],
        "riskAssessment": "low",
    }
    payload.update(overrides)
    return payload


class VerdictSchemaTests(unittest.TestCase):
    def test_valid_verdict_loads(self):
        result = validate_verdict(_verdict())
        self.assertTrue(result["approve"])
        self.assertEqual(result["riskAssessment"], "low")
        self.assertEqual(len(result["reasons"]), 2)

    def test_unknown_key_refuses_naming_it(self):
        payload = _verdict()
        payload["extraField"] = "nope"
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        self.assertIn("extraField", str(ctx.exception))

    def test_missing_key_refuses_naming_it(self):
        payload = _verdict()
        del payload["riskAssessment"]
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        self.assertIn("riskAssessment", str(ctx.exception))

    def test_approve_wrong_type_refuses(self):
        payload = _verdict(approve="true")
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        self.assertIn("verdict.approve", str(ctx.exception))

    def test_reasons_wrong_type_refuses(self):
        payload = _verdict(reasons="not a list")
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        self.assertIn("verdict.reasons", str(ctx.exception))

    def test_reasons_empty_list_refuses(self):
        payload = _verdict(reasons=[])
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        self.assertIn("verdict.reasons", str(ctx.exception))

    def test_top_level_not_an_object_refuses(self):
        with self.assertRaises(VerdictSchemaError):
            validate_verdict(["not", "an", "object"])

    def test_bad_risk_assessment_enum_refuses_naming_the_enum(self):
        """The plan's mandatory negative control, direction 1: an out-of-enum
        riskAssessment value is refused, naming both the offender and the allowed set."""
        payload = _verdict(riskAssessment="extreme")
        with self.assertRaises(VerdictSchemaError) as ctx:
            validate_verdict(payload)
        message = str(ctx.exception)
        self.assertIn("verdict.riskAssessment", message)
        self.assertIn("extreme", message)
        self.assertIn("low", message)
        self.assertIn("medium", message)
        self.assertIn("high", message)

    def test_corrected_risk_assessment_is_accepted(self):
        """Direction 2 of the same negative control: corrected to an in-enum value,
        the identical payload is accepted."""
        payload = _verdict(riskAssessment="extreme")
        with self.assertRaises(VerdictSchemaError):
            validate_verdict(payload)
        payload["riskAssessment"] = "high"
        result = validate_verdict(payload)
        self.assertEqual(result["riskAssessment"], "high")


# ══════════════════════════════════════════════════════════════════════════
# run_verify(): the orchestration seam - injectable invoke, deferred default
# ══════════════════════════════════════════════════════════════════════════


class RunVerifyOrchestrationSeamTests(unittest.TestCase):
    def setUp(self) -> None:
        self.incident = _incident()
        self.patch = "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n+fixed\n"
        self.policy = _CannedPolicy()

    def _invoke_returning(self, *, apply_exit=0, gate_results=None, verifier_raw=None, refutation=None):
        calls = []

        def invoke(incident, patch, base_sha, sandbox_root, *, model, refutation=refutation):
            calls.append(
                {
                    "incident": incident,
                    "patch": patch,
                    "base_sha": base_sha,
                    "sandbox_root": sandbox_root,
                    "model": model,
                    "refutation": refutation,
                }
            )
            return {
                "applyResult": {"exitCode": apply_exit, "stderr": "" if apply_exit == 0 else "conflict"},
                "gateResults": gate_results,
                "verifierRaw": verifier_raw,
                "refutation": {"provider": "glm", "verdict": "no refutation found"},
            }

        return invoke, calls

    def test_apply_failure_short_circuits_before_any_gate_or_verifier(self):
        invoke, calls = self._invoke_returning(apply_exit=1)

        result = run_verify(
            self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
            policy_obj=self.policy, invoke=invoke,
        )

        self.assertEqual(len(calls), 1)
        self.assertEqual(result["stage"], "apply")
        self.assertEqual(result["decision"], REJECT)
        self.assertFalse(result["approve"])
        self.assertIsNone(result["gates"])
        self.assertIsNone(result["refutation"])
        self.assertIn("D7", result["reasons"][0])

    def test_red_mechanical_gate_short_circuits_before_verifier_is_ever_consulted(self):
        bad_gates = _all_green_gate_results(
            unitTests={"label": "unit", "total": 43, "failed": 1, "failedNames": ["tst_broken"]}
        )
        # verifierRaw is deliberately a poison value (would raise if ever validated) -
        # proves run_verify() never even looks at it once the gates already rejected.
        invoke, calls = self._invoke_returning(
            apply_exit=0, gate_results=bad_gates, verifier_raw={"this": "is not a verdict"}
        )

        result = run_verify(
            self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
            policy_obj=self.policy, invoke=invoke,
        )

        self.assertEqual(result["stage"], "gates")
        self.assertEqual(result["decision"], REJECT)
        self.assertFalse(result["approve"])
        self.assertIsNone(result["refutation"])
        self.assertTrue(any("tst_broken" in r for r in result["reasons"]))
        self.assertEqual(result["gates"]["failedGates"], ["unitTestsFullPass"])

    def test_same_gate_fixed_green_plus_clean_verdict_reaches_approve(self):
        """The end-to-end pairing this slice's negative control calls for: the exact
        failing scenario above, with that one gate now green and a clean approve=True
        verdict from the (canned) Verifier -> decision=APPROVE."""
        clean_gates = _all_green_gate_results()
        clean_verdict = {
            "approve": True,
            "reasons": ["cause matches the diagnosed seam", "no adjacent-behavior risk found"],
            "riskAssessment": "low",
        }
        invoke, calls = self._invoke_returning(
            apply_exit=0, gate_results=clean_gates, verifier_raw=clean_verdict
        )

        result = run_verify(
            self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
            policy_obj=self.policy, invoke=invoke,
        )

        self.assertEqual(result["stage"], "verifier")
        self.assertEqual(result["decision"], APPROVE)
        self.assertTrue(result["approve"])
        self.assertEqual(result["riskAssessment"], "low")
        self.assertEqual(result["gates"]["overall"], "PASS")
        self.assertIsNotNone(result["refutation"])
        self.assertIn("verifierContext", result)
        self.assertEqual(find_forbidden_verifier_exhibits(result["verifierContext"]), [])

    def test_verifier_may_reject_even_with_every_mechanical_gate_green(self):
        """Ruling 4: 'It is allowed and expected to reject.' A clean mechanical gate
        matrix must not force approval - the model's own verdict still governs here."""
        clean_gates = _all_green_gate_results()
        rejecting_verdict = {
            "approve": False,
            "reasons": ["the patch fixes a symptom, not the diagnosed root cause"],
            "riskAssessment": "medium",
        }
        invoke, _ = self._invoke_returning(
            apply_exit=0, gate_results=clean_gates, verifier_raw=rejecting_verdict
        )

        result = run_verify(
            self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
            policy_obj=self.policy, invoke=invoke,
        )

        self.assertEqual(result["stage"], "verifier")
        self.assertEqual(result["decision"], REJECT)
        self.assertFalse(result["approve"])
        self.assertEqual(result["gates"]["overall"], "PASS")

    def test_model_routing_comes_from_policy_not_hard_coded(self):
        clean_gates = _all_green_gate_results()
        clean_verdict = {"approve": True, "reasons": ["clean"], "riskAssessment": "low"}
        invoke, calls = self._invoke_returning(
            apply_exit=0, gate_results=clean_gates, verifier_raw=clean_verdict
        )

        run_verify(
            self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
            policy_obj=self.policy, invoke=invoke,
        )

        self.assertEqual(calls[0]["model"], "opus")
        self.assertEqual(calls[0]["refutation"]["provider"], "glm")

    def test_default_invoke_is_deferred_and_raises_loudly(self):
        with self.assertRaises(NotImplementedError):
            verify_mod.default_invoke(
                self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
                model="opus", refutation={"provider": "glm", "thinking": "high", "advisory": True},
            )

    def test_run_verify_default_parameter_is_default_invoke(self):
        with self.assertRaises(NotImplementedError):
            run_verify(
                self.incident, self.patch, "deadbeef", "C:/arsbx/AR-verify",
                policy_obj=self.policy,
            )


if __name__ == "__main__":
    print(f"[test_autorepair_verify] script under test: {VERIFY_SCRIPT_PATH}")
    unittest.main(verbosity=2)

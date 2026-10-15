#!/usr/bin/env python3
"""test_autorepair_diagnosis.py - tests for scripts/autorepair/diagnosis.py (Guardian Loop
Slice G5: "Diagnosis - why, with citations, before any edit").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G5. Pure Python,
stdlib unittest, house flat convention (D8: tests/test_autorepair_*.py). Runnable directly:

    python tests/test_autorepair_diagnosis.py -v

HERMETIC end to end: no sandbox is built, no `claude -p` session runs, no model call is
made anywhere in this file. The citation-check tests operate on a small temp "sandbox" dir
this file creates and destroys itself (a known file of a known line count) - never a real
Guardian Loop sandbox (scripts/autorepair/sandbox.py is not imported here at all). The
orchestration tests inject a canned `invoke` callable; the one real deferred boundary
(default_invoke()) is asserted to raise NotImplementedError, never called for real.

Test groups:
  DiagnosisSchemaTests            - a valid diagnosis loads; unknown top-level key, missing
                                     top-level field, unknown/missing rootCause field, and
                                     wrong-type fields (line as a string, confidence as a
                                     non-string, wouldNeedForbiddenChange as a string) each
                                     raise DiagnosisSchemaError naming the offender.
  DiagnosisNegativeControlTests   - the plan's mandatory negative control, both directions:
                                     confidence: "certain" (not in the enum) is refused,
                                     naming the enum; corrected to "high" is accepted.
  CitationCheckTests               - against a small hermetic fixture tree: a citation to
                                     an existing file + in-range line passes (returns None);
                                     a nonexistent file refuses; an out-of-range line
                                     (too high, and zero/negative) refuses; a citation path
                                     escaping the sandbox via '..' refuses; an absolute
                                     path citation refuses.
  ForbiddenEscalationTests         - wouldNeedForbiddenChange: true signals ESCALATE;
                                     false signals PROCEED.
  ConfidenceGateTests               - low < medium (floor=medium) -> False; high under a
                                     medium floor -> True; equal (medium under medium) ->
                                     True; an unrecognized confidence value raises.
  DiagnosisOrchestrationSeamTests  - diagnose()'s injectable invoke seam: an injected
                                     canned callable drives schema + citation + escalation
                                     + confidence-gate wiring end to end without ever
                                     calling a real model; wouldNeedForbiddenChange short-
                                     circuits the confidence gate; a below-floor confidence
                                     does not proceed; default_invoke() (the DEFAULT
                                     parameter) raises NotImplementedError rather than
                                     silently doing live work - the deferred boundary
                                     proven loud, not silent.
"""
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DIAGNOSIS_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "diagnosis.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


diagnosis_mod = _load_module("autorepair_diagnosis", DIAGNOSIS_SCRIPT_PATH)

validate_diagnosis = diagnosis_mod.validate_diagnosis
check_citations = diagnosis_mod.check_citations
check_forbidden_escalation = diagnosis_mod.check_forbidden_escalation
may_proceed_to_repair = diagnosis_mod.may_proceed_to_repair
diagnose = diagnosis_mod.diagnose
DiagnosisSchemaError = diagnosis_mod.DiagnosisSchemaError
CitationError = diagnosis_mod.CitationError
ESCALATE = diagnosis_mod.ESCALATE
PROCEED = diagnosis_mod.PROCEED


def _diagnosis_payload(**overrides) -> dict:
    """A schema-valid raw diagnosis dict (module docstring's own worked example, trimmed).
    Callers pass overrides (including nested rootCause overrides via a full replacement
    dict) to build the specific malformed/negative-control cases each test needs."""
    payload = {
        "observed": "readerReady never fires; the journey step times out",
        "expected": "readerReady fires once the page-render signal completes",
        "rootCause": {
            "file": "native/engine/Foo.cpp",
            "line": 3,
            "claim": "readerReady is bound to visibility instead of the render signal",
        },
        "seam": "ComicReaderShell.qml readerReady binding",
        "confidence": "high",
        "proposedRepair": "bind readerReady to the page-render signal instead of visibility",
        "wouldNeedForbiddenChange": False,
    }
    payload.update(overrides)
    return payload


# ══════════════════════════════════════════════════════════════════════════
# validate_diagnosis() - the closed-schema gate
# ══════════════════════════════════════════════════════════════════════════


class DiagnosisSchemaTests(unittest.TestCase):
    def test_valid_diagnosis_loads(self):
        result = validate_diagnosis(_diagnosis_payload())
        self.assertEqual(result["confidence"], "high")
        self.assertEqual(result["rootCause"]["file"], "native/engine/Foo.cpp")
        self.assertEqual(result["rootCause"]["line"], 3)
        self.assertFalse(result["wouldNeedForbiddenChange"])

    def test_unknown_top_level_key_refuses_naming_it(self):
        payload = _diagnosis_payload()
        payload["extraField"] = "not part of the contract"
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("extraField", str(ctx.exception))

    def test_missing_top_level_field_refuses_naming_it(self):
        payload = _diagnosis_payload()
        del payload["seam"]
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("seam", str(ctx.exception))

    def test_unknown_root_cause_key_refuses_naming_it(self):
        payload = _diagnosis_payload()
        payload["rootCause"] = {**payload["rootCause"], "bogus": "x"}
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("bogus", str(ctx.exception))

    def test_missing_root_cause_field_refuses_naming_it(self):
        payload = _diagnosis_payload()
        root_cause = dict(payload["rootCause"])
        del root_cause["line"]
        payload["rootCause"] = root_cause
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("line", str(ctx.exception))

    def test_line_as_string_refuses_wrong_type(self):
        payload = _diagnosis_payload()
        payload["rootCause"] = {**payload["rootCause"], "line": "3"}
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("rootCause.line", str(ctx.exception))

    def test_would_need_forbidden_change_as_string_refuses_wrong_type(self):
        payload = _diagnosis_payload(wouldNeedForbiddenChange="false")
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        self.assertIn("wouldNeedForbiddenChange", str(ctx.exception))

    def test_top_level_not_an_object_refuses(self):
        with self.assertRaises(DiagnosisSchemaError):
            validate_diagnosis(["not", "an", "object"])


# ══════════════════════════════════════════════════════════════════════════
# Negative control (mandatory, both directions): the confidence enum
# ══════════════════════════════════════════════════════════════════════════


class DiagnosisNegativeControlTests(unittest.TestCase):
    """The plan's mandatory negative control (Slice G5's own "Focused tests" contract):
    'canned diagnosis JSON with confidence: "certain" (not in enum) -> refused; corrected
    -> accepted.'"""

    def test_confidence_certain_is_refused_naming_the_enum(self):
        payload = _diagnosis_payload(confidence="certain")
        with self.assertRaises(DiagnosisSchemaError) as ctx:
            validate_diagnosis(payload)
        message = str(ctx.exception)
        # The exact assertion: the refusal names BOTH the offending field and the
        # allowed enum values - "certain" is not silently coerced or dropped, and the
        # caller can see exactly what would have been accepted.
        self.assertIn("diagnosis.confidence", message)
        self.assertIn("high", message)
        self.assertIn("medium", message)
        self.assertIn("low", message)
        self.assertIn("certain", message)

    def test_confidence_corrected_to_high_is_accepted(self):
        payload = _diagnosis_payload(confidence="high")
        result = validate_diagnosis(payload)
        self.assertEqual(result["confidence"], "high")


# ══════════════════════════════════════════════════════════════════════════
# check_citations() - the F0-contract citation check
# ══════════════════════════════════════════════════════════════════════════


class CitationCheckTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g5citations_")
        self.tmp_path = Path(self._tmp.name)
        self.sandbox = self.tmp_path / "sandbox"
        (self.sandbox / "native" / "engine").mkdir(parents=True)
        # A known file of a known line count (5 lines) - the fixture the plan's own
        # instructions call for: "a small hermetic fixture tree ... with a known file of
        # known length."
        self.cited_file = self.sandbox / "native" / "engine" / "Foo.cpp"
        self.cited_file.write_text("line1\nline2\nline3\nline4\nline5\n", encoding="utf-8")
        # A sibling OUTSIDE the sandbox - the escape target for the '..' test.
        self.outside = self.tmp_path / "outside"
        self.outside.mkdir()
        (self.outside / "secret.cpp").write_text("top secret\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _diagnosis(self, *, file: str, line: int) -> dict:
        return validate_diagnosis(_diagnosis_payload(
            rootCause={"file": file, "line": line, "claim": "test claim"}
        ))

    def test_existing_file_in_range_line_passes(self):
        diagnosis = self._diagnosis(file="native/engine/Foo.cpp", line=3)
        # No exception -> pass. check_citations() returns None on success.
        self.assertIsNone(check_citations(diagnosis, self.sandbox))

    def test_boundary_lines_pass(self):
        """Line 1 and the LAST line (5) are both in-range - off-by-one check both ends."""
        diagnosis_first = self._diagnosis(file="native/engine/Foo.cpp", line=1)
        diagnosis_last = self._diagnosis(file="native/engine/Foo.cpp", line=5)
        self.assertIsNone(check_citations(diagnosis_first, self.sandbox))
        self.assertIsNone(check_citations(diagnosis_last, self.sandbox))

    def test_nonexistent_file_refuses(self):
        diagnosis = self._diagnosis(file="native/engine/DoesNotExist.cpp", line=1)
        with self.assertRaises(CitationError) as ctx:
            check_citations(diagnosis, self.sandbox)
        self.assertIn("does not exist", str(ctx.exception))

    def test_out_of_range_line_too_high_refuses(self):
        diagnosis = self._diagnosis(file="native/engine/Foo.cpp", line=999)
        with self.assertRaises(CitationError) as ctx:
            check_citations(diagnosis, self.sandbox)
        self.assertIn("out of range", str(ctx.exception))

    def test_path_escaping_sandbox_via_dotdot_refuses(self):
        diagnosis = self._diagnosis(file="../outside/secret.cpp", line=1)
        with self.assertRaises(CitationError) as ctx:
            check_citations(diagnosis, self.sandbox)
        self.assertIn("..", str(ctx.exception))

    def test_absolute_path_citation_refuses(self):
        diagnosis = self._diagnosis(file=str(self.outside / "secret.cpp"), line=1)
        with self.assertRaises(CitationError) as ctx:
            check_citations(diagnosis, self.sandbox)
        self.assertIn("sandbox-relative", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# check_forbidden_escalation() - the stop-law reflex, mechanized
# ══════════════════════════════════════════════════════════════════════════


class ForbiddenEscalationTests(unittest.TestCase):
    def test_would_need_forbidden_change_true_escalates(self):
        diagnosis = validate_diagnosis(_diagnosis_payload(wouldNeedForbiddenChange=True))
        result = check_forbidden_escalation(diagnosis)
        self.assertEqual(result["decision"], ESCALATE)

    def test_would_need_forbidden_change_false_proceeds(self):
        diagnosis = validate_diagnosis(_diagnosis_payload(wouldNeedForbiddenChange=False))
        result = check_forbidden_escalation(diagnosis)
        self.assertEqual(result["decision"], PROCEED)


# ══════════════════════════════════════════════════════════════════════════
# may_proceed_to_repair() - the confidence gate
# ══════════════════════════════════════════════════════════════════════════


class ConfidenceGateTests(unittest.TestCase):
    def test_low_under_medium_floor_is_false(self):
        self.assertFalse(may_proceed_to_repair("low", "medium"))

    def test_high_under_medium_floor_is_true(self):
        self.assertTrue(may_proceed_to_repair("high", "medium"))

    def test_medium_equal_to_medium_floor_is_true(self):
        self.assertTrue(may_proceed_to_repair("medium", "medium"))

    def test_low_under_low_floor_is_true(self):
        self.assertTrue(may_proceed_to_repair("low", "low"))

    def test_medium_under_high_floor_is_false(self):
        self.assertFalse(may_proceed_to_repair("medium", "high"))

    def test_unrecognized_confidence_raises(self):
        with self.assertRaises(DiagnosisSchemaError):
            may_proceed_to_repair("certain", "medium")

    def test_unrecognized_min_confidence_raises(self):
        with self.assertRaises(DiagnosisSchemaError):
            may_proceed_to_repair("high", "extreme")


# ══════════════════════════════════════════════════════════════════════════
# diagnose(): the orchestration seam - injectable invoke, deferred default
# ══════════════════════════════════════════════════════════════════════════


class _CannedPolicy:
    """A minimal stand-in for scripts/autorepair/policy.py's Policy dataclass - only the
    two fields diagnose() actually reads (policy.modelRouting.diagnosis,
    policy.minConfidenceToRepair). Avoids depending on the real docs/autorepair/
    policy.json in these hermetic tests, exactly as triage.py's own tests pass an
    explicit policy_obj where they need one pinned rather than relying on the shipped
    default."""

    def __init__(self, *, min_confidence: str = "medium"):
        self.policy = {
            "modelRouting": {"diagnosis": "opus", "repair": "sonnet", "verify": "opus"},
            "minConfidenceToRepair": min_confidence,
        }


class DiagnosisOrchestrationSeamTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g5diagnose_")
        self.sandbox = Path(self._tmp.name) / "sandbox"
        (self.sandbox / "native" / "engine").mkdir(parents=True)
        (self.sandbox / "native" / "engine" / "Foo.cpp").write_text(
            "line1\nline2\nline3\n", encoding="utf-8"
        )
        self.incident = {"id": "AR-test-0005", "baseSha": "deadbeef"}

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _canned_invoke(self, payload):
        calls = []

        def invoke(incident, sandbox_root, *, model):
            calls.append((incident, sandbox_root, model))
            return payload

        return invoke, calls

    def test_injected_invoke_drives_a_full_diagnosis(self):
        payload = _diagnosis_payload(
            rootCause={"file": "native/engine/Foo.cpp", "line": 2, "claim": "test claim"},
            confidence="high",
            wouldNeedForbiddenChange=False,
        )
        invoke, calls = self._canned_invoke(payload)

        result = diagnose(
            self.incident, self.sandbox,
            policy_obj=_CannedPolicy(min_confidence="medium"), invoke=invoke,
        )

        self.assertEqual(len(calls), 1, "invoke must be called exactly once")
        called_incident, called_sandbox, called_model = calls[0]
        self.assertIs(called_incident, self.incident)
        self.assertEqual(str(called_sandbox), str(self.sandbox))
        self.assertEqual(called_model, "opus", "model routing must come from policy, not be hard-coded")

        self.assertEqual(result["incidentId"], "AR-test-0005")
        self.assertFalse(result["escalate"])
        self.assertTrue(result["mayProceedToRepair"])
        self.assertEqual(result["confidence"], "high")

    def test_forbidden_change_escalation_short_circuits_confidence_gate(self):
        """Even a HIGH-confidence diagnosis must not proceed to repair once
        wouldNeedForbiddenChange is true - the stop-law reflex outranks confidence."""
        payload = _diagnosis_payload(
            rootCause={"file": "native/engine/Foo.cpp", "line": 1, "claim": "test claim"},
            confidence="high",
            wouldNeedForbiddenChange=True,
        )
        invoke, _ = self._canned_invoke(payload)

        result = diagnose(
            self.incident, self.sandbox,
            policy_obj=_CannedPolicy(min_confidence="medium"), invoke=invoke,
        )

        self.assertTrue(result["escalate"])
        self.assertEqual(result["mayProceedToRepair"], False)

    def test_low_confidence_below_floor_does_not_proceed(self):
        payload = _diagnosis_payload(
            rootCause={"file": "native/engine/Foo.cpp", "line": 1, "claim": "test claim"},
            confidence="low",
            wouldNeedForbiddenChange=False,
        )
        invoke, _ = self._canned_invoke(payload)

        result = diagnose(
            self.incident, self.sandbox,
            policy_obj=_CannedPolicy(min_confidence="medium"), invoke=invoke,
        )

        self.assertFalse(result["escalate"])
        self.assertFalse(result["mayProceedToRepair"])

    def test_bad_citation_from_invoke_refuses_before_gates_run(self):
        payload = _diagnosis_payload(
            rootCause={"file": "native/engine/Missing.cpp", "line": 1, "claim": "test claim"},
        )
        invoke, _ = self._canned_invoke(payload)

        with self.assertRaises(CitationError):
            diagnose(
                self.incident, self.sandbox,
                policy_obj=_CannedPolicy(min_confidence="medium"), invoke=invoke,
            )

    def test_default_invoke_is_deferred_and_raises_loudly(self):
        """The DEFERRED boundary must be loud, not silent: calling the real (unwired)
        live Opus invocation raises NotImplementedError rather than fabricating a
        diagnosis."""
        with self.assertRaises(NotImplementedError):
            diagnosis_mod.default_invoke(self.incident, self.sandbox, model="opus")

    def test_diagnose_default_parameter_is_default_invoke(self):
        """diagnose() with no invoke override must reach for the deferred seam, not a
        silently-working stub - proves the wiring, not just the standalone function."""
        with self.assertRaises(NotImplementedError):
            diagnose(self.incident, self.sandbox, policy_obj=_CannedPolicy())


if __name__ == "__main__":
    print(f"[test_autorepair_diagnosis] script under test: {DIAGNOSIS_SCRIPT_PATH}")
    unittest.main(verbosity=2)

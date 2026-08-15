#!/usr/bin/env python3
"""test_autorepair_document_only.py - tests for the "document-only" autonomy level
(policy.py's AUTONOMY_LEVELS admission + orchestrator.py's DOCUMENTED terminal).

docs/superpowers/plans/2026-08-15-colosseum-night-watch-n0-plan.md. Hemanth
directive 2026-08-15: "can we have the night watch and guardian loop only for
documenting bugs rather than fixing them for now?" - these tests pin the THREE
properties that directive translates to mechanically:

  1. document-only stops AFTER diagnosis gates pass and BEFORE repair - the
     repair/verify/promotion stage runners are never invoked.
  2. bug.md lands next to the stage files, and report.md carries DOCUMENTED.
  3. the OTHER terminal paths stay untouched: a dismissed triage still
     TRIAGE-DISMISSES (document-only must not resurrect dismissed noise), an
     escalated diagnosis still ESCALATEs, and draft-pr/patch-only modes still
     walk into repair exactly as before (the mode cuts only one edge).

Pure Python, stdlib unittest, house flat convention; hermetic canned stage
runners (mirrors test_autorepair_orchestrator.py's own _Spy pattern - this file
deliberately re-declares its tiny helpers rather than importing across test
files, matching the one-file-one-module flat convention).

    python tests/test_autorepair_document_only.py -v
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ORCH_PATH = REPO_ROOT / "scripts" / "autorepair" / "orchestrator.py"
POLICY_PATH = REPO_ROOT / "scripts" / "autorepair" / "policy.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


orch = _load_module("autorepair_orchestrator_doc_only", ORCH_PATH)
pol = _load_module("autorepair_policy_doc_only", POLICY_PATH)


class _Spy:
    def __init__(self, result: dict):
        self.result = result
        self.calls: list[dict] = []

    def __call__(self, incident, incident_dir, policy_obj, *, prior):
        self.calls.append({"prior": sorted(prior)})
        return self.result


def _never_called(stage: str):
    def _tripwire(incident, incident_dir, policy_obj, *, prior):
        raise AssertionError(f"{stage} must never run in document-only mode")
    return _tripwire


class _FakePolicy:
    """Carries the fields _run_loop()/_execute_stage() read, including the one this
    slice added: autonomyLevel."""

    def __init__(self, *, autonomy_level: str, per_stage=None, per_incident=10_000_000):
        self.policy = {
            "autonomyLevel": autonomy_level,
            "perStageTimeoutSec": per_stage or {stage: 10_000 for stage in orch.LOOP_STAGES},
            "perIncidentTotalSec": per_incident,
            "minConfidenceToRepair": "medium",
        }


def _incident_dict() -> dict:
    return {
        "schema": 1, "id": "AR-doc-0001", "baseSha": "deadbeef",
        "scenario": "tests/lanista_scenarios/journey_open_manga.json",
        "failingStep": {"index": 14, "label": "readerReady wait", "status": "FAIL"},
    }


def _triage_dict(verdict: str = "CONFIRMED") -> dict:
    return {
        "verdict": verdict, "reproduced": verdict == "CONFIRMED",
        "runs": [{"status": "FAIL", "stepLabel": "readerReady wait"}] * 3,
        "failingStepConsistency": {"applicable": True, "count": 3, "totalRuns": 3, "confirmThreshold": 2},
        "incidentId": "AR-doc-0001",
    }


def _diagnosis_dict(*, escalate: bool = False, may_proceed: bool = True) -> dict:
    return {
        "observed": "the readiness wait times out", "expected": "readiness fires",
        "rootCause": {"file": "qml/x.qml", "line": 208, "claim": "unsatisfiable term"},
        "seam": "the readiness contract", "confidence": "high",
        "proposedRepair": "restore the equality",
        "wouldNeedForbiddenChange": False, "incidentId": "AR-doc-0001",
        "escalate": escalate, "escalateReason": "", "mayProceedToRepair": may_proceed,
    }


class DocumentOnlyTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="doconly_")
        self.dir = Path(self._tmp.name)
        (self.dir / "incident.json").write_text(
            json.dumps(_incident_dict()), encoding="utf-8"
        )

    def tearDown(self):
        self._tmp.cleanup()

    def test_confirmed_diagnosed_incident_terminates_documented_with_bug_md(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = orch.run_incident(
            self.dir, stage_runners=runners,
            policy_obj=_FakePolicy(autonomy_level="document-only"),
        )
        self.assertEqual(outcome["terminalState"], "DOCUMENTED")
        self.assertEqual(outcome["ranStages"], ["triage", "diagnosis"])
        bug = (self.dir / "bug.md").read_text(encoding="utf-8")
        self.assertIn("# Bug - AR-doc-0001", bug)
        self.assertIn("DOCUMENTED ONLY", bug)
        report = (self.dir / "report.md").read_text(encoding="utf-8")
        self.assertIn("DOCUMENTED", report)
        self.assertIn("bug.md", report)

    def test_dismissed_triage_still_triage_dismissed_in_document_only(self):
        """The mode cuts only the walk toward repair - triage's own law is upstream
        and must be untouched: noise is still dismissed, never documented."""
        runners = {
            "triage": _Spy(_triage_dict(verdict="INFRA")),
            "diagnosis": _never_called("diagnosis"),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = orch.run_incident(
            self.dir, stage_runners=runners,
            policy_obj=_FakePolicy(autonomy_level="document-only"),
        )
        self.assertEqual(outcome["terminalState"], "TRIAGE-DISMISSED")
        self.assertFalse((self.dir / "bug.md").exists())

    def test_escalated_diagnosis_still_escalates_in_document_only(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict(escalate=True, may_proceed=False)),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = orch.run_incident(
            self.dir, stage_runners=runners,
            policy_obj=_FakePolicy(autonomy_level="document-only"),
        )
        self.assertEqual(outcome["terminalState"], "ESCALATE")
        self.assertFalse((self.dir / "bug.md").exists())

    def test_draft_pr_mode_still_walks_into_repair(self):
        """The control direction: with the SAME runners but autonomyLevel draft-pr,
        repair runs - proving document-only's cut is the only difference."""
        repair_spy = _Spy({"accepted": False, "escalateReason": "exhausted attempts"})
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": repair_spy,
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = orch.run_incident(
            self.dir, stage_runners=runners,
            policy_obj=_FakePolicy(autonomy_level="draft-pr"),
        )
        self.assertEqual(len(repair_spy.calls), 1)
        self.assertNotEqual(outcome["terminalState"], "DOCUMENTED")

    def test_documented_is_a_first_class_terminal_state(self):
        self.assertIn("DOCUMENTED", orch.TERMINAL_STATES)
        # render_report accepts it (the unknown-state refusal must not fire)
        body = orch.render_report(
            _incident_dict(), "DOCUMENTED", "detail", {"triage": {}, "diagnosis": {}},
            generated_at=__import__("datetime").datetime(2026, 8, 15, tzinfo=__import__("datetime").timezone.utc),
        )
        self.assertIn("# Guardian Loop incident AR-doc-0001 - DOCUMENTED", body)


class PolicyAdmissionTests(unittest.TestCase):
    def test_document_only_and_glm_are_admitted(self):
        self.assertIn("document-only", pol.AUTONOMY_LEVELS)
        self.assertIn("glm", pol.MODEL_TIERS)

    def test_shipped_law_loads_with_document_only(self):
        """The shipped docs/autorepair/policy.json must actually validate under the
        new enums (this slice flipped it to document-only + glm routing + gate on)."""
        loaded = pol.load_policy()
        self.assertEqual(loaded.policy["autonomyLevel"], "document-only")
        self.assertTrue(loaded.policy["nightWatchAutoRepair"])
        self.assertEqual(
            loaded.policy["modelRouting"],
            {"diagnosis": "glm", "repair": "glm", "verify": "glm"},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""test_autorepair_orchestrator.py - tests for scripts/autorepair/orchestrator.py
(Guardian Loop Slice G9: "The orchestrator + the founding end-to-end run").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G9. Pure Python,
stdlib unittest, house flat convention (D8: tests/test_autorepair_*.py). Runnable directly:

    python tests/test_autorepair_orchestrator.py -v

HERMETIC end to end: no sandbox is built, no `claude -p` session runs, no network call is
made, no real git/gh command runs against the MAIN repo. Every incident directory is a
`tempfile.TemporaryDirectory()` fixture; every stage_runners[...] callable a test injects is
a canned Python function returning a hand-built dict - never live work. The ONE piece of
real, deliberate I/O this file exercises is owner.lock's pid-liveness check
(`orchestrator._pid_is_alive`, backed by a real `tasklist` call) - proven against a REAL
subprocess (a genuinely live pid, then a genuinely dead one after it exits), never mocked,
matching the house preference for real mechanics over faked ones (see
test_autorepair_triage.py's own real NTFS-junction guard-hook test for precedent).

Test groups:
  StageFileValidityTests           - is_stage_complete()/first_incomplete_stage() against
                                      canned stage files: missing, malformed, and valid
                                      shapes for every stage, including the "incident"
                                      precondition stage itself.
  FreshIncidentRunTests             - a brand-new incident (only incident.json present)
                                      drives the full CONFIRMED -> not-escalated ->
                                      accepted -> APPROVE -> promoted chain via injected
                                      stage_runners, patch-only mode reaching
                                      PROMOTION-READY and draft-pr mode reaching PROMOTED.
  TerminalStateTests                - every one of the five OTHER terminal-state triggers,
                                      each isolated: FLAKY/INFRA triage -> TRIAGE-DISMISSED
                                      (repair/verify/promotion never invoked);
                                      wouldNeedForbiddenChange and below-floor-confidence
                                      diagnosis -> ESCALATE (repair never invoked); ordinary
                                      repair exhaustion -> BUDGET; oversized-patch repair
                                      exhaustion -> ESCALATE; Verifier REJECT -> ESCALATE;
                                      a stage exceeding its perStageTimeoutSec cap and an
                                      incident exceeding perIncidentTotalSec -> BUDGET (via
                                      a fake stepping clock, no real sleep); a
                                      sandbox.DriftViolation raised mid-stage -> VIOLATION;
                                      a draft-pr promotion whose `gh` step is Bridge blocked
                                      still reaches PROMOTED with that noted honestly, never
                                      silently claiming a PR exists.
  ResumeTests                       - triage.json+diagnosis.json already present resumes
                                      at repair (triage/diagnosis stage_runners spied to
                                      assert zero calls); every stage file present (incl.
                                      report.md) resumes with NOTHING running at all.
  NegativeControlTests              - the plan's mandatory negative control, both
                                      directions, exact assertion documented inline: delete
                                      triage.json from a chain that has NOT yet reached
                                      report.md -> resume RE-RUNS triage (not skipped) and
                                      the cascade re-runs every stage after it too, never
                                      jumping straight to diagnosis with stale trust; a
                                      second resume call once report.md exists (the
                                      "restore" direction) runs nothing at all.
  ReportRenderingTests               - render_report() for every one of the six terminal
                                      states: contains its own marker in the H1 header, no
                                      emoji/pictographic characters, non-trivial Hemanth-
                                      language body; an unknown terminal state name is
                                      refused.
  BudgetAccountingTests              - _execute_stage() directly, pure given a fake
                                      stepping clock: a stage under its cap passes through
                                      unchanged; a stage over its cap raises
                                      StageBudgetExceeded; the incident-total cap raising
                                      IncidentBudgetExceeded even when the individual
                                      stage's own cap was not exceeded.
  LockTests                          - acquire_lock()/release_lock() unit-level: the
                                      pid/path/createdAt triple round-trips through JSON; a
                                      second acquire against a REAL live subprocess pid is
                                      refused; once that subprocess exits, the now-dead pid
                                      is silently reclaimed; release_lock() never deletes a
                                      lock some other pid has since reclaimed.
  LockIntegrationTests                - run_incident() itself refuses when owner.lock is
                                      held by a real live process, and cleanly releases its
                                      own lock (file gone) after a normal completed run.
  DeferredStageRunnerTests            - every DEFAULT_STAGE_RUNNERS entry raises
                                      NotImplementedError rather than silently doing live
                                      work - the deferred boundary proven loud, not silent
                                      (mirrors triage.py's/diagnosis.py's/
                                      repair_contract.py's/verify.py's/promotion.py's own
                                      identical test for their own default_invoke()/
                                      default_run_once()).
  CliArgumentTests                   - the CLI's own argument-validation refusals:
                                      --incident without --resume, and --resume against a
                                      nonexistent incident directory.
"""
from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "orchestrator.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_module("autorepair_orchestrator", SCRIPT_PATH)


# ══════════════════════════════════════════════════════════════════════════
# Fixture builders - canned stage files, shaped like the real sibling modules'
# own return values, but hand-built (never a real triage()/diagnose()/... call)
# ══════════════════════════════════════════════════════════════════════════


def _write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2), encoding="utf-8")


def _incident_dict(incident_id: str = "AR-test-0001", **overrides) -> dict:
    base = {
        "schema": 1,
        "id": incident_id,
        "baseSha": "deadbeef",
        "scenario": "tests/lanista_scenarios/journey_open_manga.json",
        "failingStep": {"index": 25, "label": "readerReady wait", "status": "FAIL"},
    }
    base.update(overrides)
    return base


def _write_incident(incident_dir: Path, incident_id: str = "AR-test-0001", **overrides) -> dict:
    obj = _incident_dict(incident_id, **overrides)
    _write_json(incident_dir / "incident.json", obj)
    return obj


def _triage_dict(*, verdict: str = "CONFIRMED", reproduced: bool = True, **overrides) -> dict:
    base = {
        "verdict": verdict,
        "runs": [{"status": "FAIL", "stepLabel": "readerReady wait"}] * 3,
        "failingStepConsistency": {"applicable": True, "label": "readerReady wait", "count": 3},
        "incidentId": "AR-test-0001",
        "reproduced": reproduced,
    }
    base.update(overrides)
    return base


def _diagnosis_dict(
    *, confidence: str = "high", would_need_forbidden: bool = False,
    escalate: bool = False, may_proceed: bool = True, **overrides,
) -> dict:
    base = {
        "observed": "readerReady never fires",
        "expected": "readerReady fires once the page-render signal completes",
        "rootCause": {"file": "qml/reader/ComicReaderShell.qml", "line": 42, "claim": "bound to visibility"},
        "seam": "ComicReaderShell.qml readerReady binding",
        "confidence": confidence,
        "proposedRepair": "bind readerReady to the page-render signal",
        "wouldNeedForbiddenChange": would_need_forbidden,
        "incidentId": "AR-test-0001",
        "escalate": escalate,
        "escalateReason": "wouldNeedForbiddenChange is false - no forbidden-path escalation needed",
        "mayProceedToRepair": may_proceed,
    }
    base.update(overrides)
    return base


def _repair_dict(*, accepted: bool = True, escalate_reason: str = "", **overrides) -> dict:
    base = {
        "accepted": accepted,
        "incidentId": "AR-test-0001",
        "attempts": 1,
        "attemptLog": [{"attempt": 1, "accepted": accepted}],
    }
    if accepted:
        base.update({
            "classification": {
                "testAdds": ["tests/test_reader_ready.py"],
                "forbidden": [], "production": ["qml/reader/ComicReaderShell.qml"],
            },
            "patchLineCount": 12,
            "bugtest": {"cmd": "ctest", "args": ["-R", "test_reader_ready"], "expectRedWithoutFix": True},
            "redExitCodes": [1, 1], "greenExitCodes": [0, 0],
        })
    else:
        base["escalateReason"] = escalate_reason or "exhausted policy.maxRepairAttempts=3 without an accepted patch"
    base.update(overrides)
    return base


def _verdict_dict(*, decision: str = "APPROVE", **overrides) -> dict:
    base = {
        "incidentId": "AR-test-0001",
        "stage": "verifier",
        "decision": decision,
        "approve": decision == "APPROVE",
        "reasons": ["clean"] if decision == "APPROVE" else ["bug test meaningfulness in doubt"],
        "riskAssessment": "low",
        "gates": {"overall": "PASS", "gates": {}, "failedGates": []},
        "refutation": {"provider": "glm", "argues": "none"},
    }
    base.update(overrides)
    return base


def _promotion_dict(*, mode: str = "patch-only", pr_status: str = "created", **overrides) -> dict:
    if mode == "patch-only":
        base = {
            "mode": "patch-only", "incidentId": "AR-test-0001",
            "targetBranch": "autorepair/AR-test-0001",
            "promotionReadyContent": "# PROMOTION READY", "prUrl": None,
        }
    else:
        base = {
            "mode": "draft-pr", "incidentId": "AR-test-0001",
            "targetBranch": "autorepair/AR-test-0001",
            "branchPushed": True,
            "prUrl": "https://github.com/example/colosseum/pull/1" if pr_status == "created" else None,
            "prStatus": pr_status,
            "prBodyFilePath": None if pr_status == "created" else "artifacts/autorepair/AR-test-0001/pr-body.md",
            "rebaseFlag": None,
        }
    base.update(overrides)
    return base


class _Spy:
    """A tiny call-recording stage_runners[...] callable. Returns a canned dict from
    `results` (a list consumed in call order, or a single dict reused for every call);
    raises AssertionError if called more times than expected, so a test can assert
    "this stage_runner was never invoked" simply by never seeding it with a result."""

    def __init__(self, result=None, results=None):
        self.calls: list[dict] = []
        self._result = result
        self._results = list(results) if results is not None else None

    def __call__(self, incident, incident_dir, policy_obj, *, prior):
        self.calls.append({"incidentId": incident.get("id"), "prior": sorted(prior)})
        if self._results is not None:
            if not self._results:
                raise AssertionError("_Spy called more times than results were seeded")
            return self._results.pop(0)
        if self._result is None:
            raise AssertionError("_Spy was called but must not have been (no result seeded)")
        return self._result


def _never_called(stage: str):
    def _fn(*args, **kwargs):
        raise AssertionError(f"stage_runners[{stage!r}] must not have been called")
    return _fn


# ══════════════════════════════════════════════════════════════════════════
# Stage-file validity + first_incomplete_stage()
# ══════════════════════════════════════════════════════════════════════════


class StageFileValidityTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9stage_")
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_empty_dir_first_incomplete_is_incident(self):
        self.assertEqual(mod.first_incomplete_stage(self.dir), "incident")
        self.assertFalse(mod.is_stage_complete(self.dir, "incident"))

    def test_incident_present_first_incomplete_is_triage(self):
        _write_incident(self.dir)
        self.assertTrue(mod.is_stage_complete(self.dir, "incident"))
        self.assertEqual(mod.first_incomplete_stage(self.dir), "triage")

    def test_malformed_triage_file_is_incomplete(self):
        _write_incident(self.dir)
        _write_json(self.dir / "triage.json", {"verdict": "MAYBE"})  # not a real enum value
        self.assertFalse(mod.is_stage_complete(self.dir, "triage"))
        self.assertEqual(mod.first_incomplete_stage(self.dir), "triage")

    def test_valid_triage_confirms_completeness_and_advances(self):
        _write_incident(self.dir)
        _write_json(self.dir / "triage.json", _triage_dict())
        self.assertTrue(mod.is_stage_complete(self.dir, "triage"))
        self.assertEqual(mod.first_incomplete_stage(self.dir), "diagnosis")

    def test_diagnosis_missing_required_key_is_incomplete(self):
        _write_incident(self.dir)
        _write_json(self.dir / "triage.json", _triage_dict())
        bad = _diagnosis_dict()
        del bad["escalate"]  # orchestration field the stage file must carry
        _write_json(self.dir / "diagnosis.json", bad)
        self.assertFalse(mod.is_stage_complete(self.dir, "diagnosis"))

    def test_repair_file_requires_boolean_accepted(self):
        _write_incident(self.dir)
        _write_json(self.dir / "repair.json", {"accepted": "yes"})
        self.assertFalse(mod.is_stage_complete(self.dir, "repair"))
        _write_json(self.dir / "repair.json", _repair_dict())
        self.assertTrue(mod.is_stage_complete(self.dir, "repair"))

    def test_verdict_file_requires_known_decision(self):
        _write_incident(self.dir)
        _write_json(self.dir / "verdict.json", {"decision": "MAYBE"})
        self.assertFalse(mod.is_stage_complete(self.dir, "verify"))
        _write_json(self.dir / "verdict.json", _verdict_dict())
        self.assertTrue(mod.is_stage_complete(self.dir, "verify"))

    def test_report_md_empty_file_is_incomplete(self):
        (self.dir / "report.md").write_text("   \n", encoding="utf-8")
        self.assertFalse(mod.is_stage_complete(self.dir, "promotion"))
        (self.dir / "report.md").write_text("# something\n", encoding="utf-8")
        self.assertTrue(mod.is_stage_complete(self.dir, "promotion"))

    # ── F1 hardening (Guardian Loop audit, CRITICAL) ────────────────────────
    # The old validators only checked `decision in (REJECT, APPROVE)` and `accepted is
    # bool` - a thin/tampered stage file was then TRUSTED on resume and the loop walked
    # straight to Promotion without the gates ever being re-derived. These prove the
    # bypass is closed: a shallow file no longer satisfies is_stage_complete().

    def test_bare_decision_only_verdict_is_invalid(self):
        self.assertFalse(mod._is_valid_verdict_file({"decision": "APPROVE"}))

    def test_bare_decision_only_verdict_stage_reruns_on_resume(self):
        """The exact resume-facing assertion the audit named: a bare {"decision":
        "APPROVE"} verdict.json must make is_stage_complete(..., "verify") False, so the
        verify stage RE-RUNS on resume rather than being trusted."""
        _write_incident(self.dir)
        _write_json(self.dir / "verdict.json", {"decision": "APPROVE"})
        self.assertFalse(mod.is_stage_complete(self.dir, "verify"))

    def test_inconsistent_approve_gates_mismatch_verdict_is_invalid(self):
        """decision=APPROVE but approve=True/gates.overall=FAIL disagree - a tampered or
        hand-edited file must never be trusted just because `decision` alone parses."""
        obj = {
            "decision": "APPROVE", "approve": True,
            "gates": {"overall": "FAIL"}, "reasons": [],
        }
        self.assertFalse(mod._is_valid_verdict_file(obj))

    def test_decision_approve_but_approve_false_is_invalid(self):
        obj = {
            "decision": "APPROVE", "approve": False,
            "gates": {"overall": "PASS"}, "reasons": ["x"],
        }
        self.assertFalse(mod._is_valid_verdict_file(obj))

    def test_full_consistent_approve_verdict_is_valid(self):
        self.assertTrue(mod._is_valid_verdict_file(_verdict_dict(decision="APPROVE")))

    def test_full_consistent_reject_verdict_is_valid(self):
        self.assertTrue(mod._is_valid_verdict_file(_verdict_dict(decision="REJECT")))

    def test_verdict_missing_gates_key_is_invalid(self):
        obj = _verdict_dict(decision="APPROVE")
        del obj["gates"]
        self.assertFalse(mod._is_valid_verdict_file(obj))

    def test_verdict_reasons_not_a_list_is_invalid(self):
        obj = _verdict_dict(decision="APPROVE")
        obj["reasons"] = "clean"
        self.assertFalse(mod._is_valid_verdict_file(obj))

    def test_bare_accepted_only_repair_is_invalid(self):
        """A thin {"accepted": true} with none of the accepted-only evidence fields
        (classification/bugtest/redExitCodes/greenExitCodes) must be invalid - the exact
        shallow-trust bypass the audit named."""
        self.assertFalse(mod._is_valid_repair_file({"accepted": True}))

    def test_bare_accepted_only_repair_stage_reruns_on_resume(self):
        _write_incident(self.dir)
        _write_json(self.dir / "repair.json", {"accepted": True})
        self.assertFalse(mod.is_stage_complete(self.dir, "repair"))

    def test_full_accepted_repair_is_valid(self):
        self.assertTrue(mod._is_valid_repair_file(_repair_dict(accepted=True)))

    def test_rejected_repair_needs_only_accepted_bool(self):
        """A rejected/escalated repair (accepted=False) carries no accepted-only
        evidence fields by design - the stricter shape check must apply ONLY to
        accepted=True, never demand evidence fields a rejection never produces."""
        self.assertTrue(mod._is_valid_repair_file(_repair_dict(accepted=False)))

    def test_accepted_repair_missing_one_evidence_field_is_invalid(self):
        obj = _repair_dict(accepted=True)
        del obj["redExitCodes"]
        self.assertFalse(mod._is_valid_repair_file(obj))

    def test_every_stage_present_first_incomplete_is_none(self):
        _write_incident(self.dir)
        _write_json(self.dir / "triage.json", _triage_dict())
        _write_json(self.dir / "diagnosis.json", _diagnosis_dict())
        _write_json(self.dir / "repair.json", _repair_dict())
        _write_json(self.dir / "verdict.json", _verdict_dict())
        (self.dir / "report.md").write_text("# Guardian Loop incident AR-test-0001 - PROMOTED\n", encoding="utf-8")
        self.assertIsNone(mod.first_incomplete_stage(self.dir))


# ══════════════════════════════════════════════════════════════════════════
# Fresh incident: the full chain to PROMOTION-READY / PROMOTED
# ══════════════════════════════════════════════════════════════════════════


class FreshIncidentRunTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9fresh_")
        self.dir = Path(self._tmp.name)
        _write_incident(self.dir)

    def tearDown(self):
        self._tmp.cleanup()

    def _runners(self, promotion_result):
        return {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict()),
            "verify": _Spy(_verdict_dict()),
            "promotion": _Spy(promotion_result),
        }

    def test_fresh_incident_reaches_promotion_ready_in_patch_only_mode(self):
        runners = self._runners(_promotion_dict(mode="patch-only"))
        outcome = mod.run_incident(self.dir, stage_runners=runners)

        self.assertEqual(outcome["terminalState"], "PROMOTION-READY")
        self.assertFalse(outcome["alreadyTerminal"])
        self.assertEqual(outcome["ranStages"], ["triage", "diagnosis", "repair", "verify", "promotion"])
        for stage in mod.LOOP_STAGES:
            self.assertEqual(len(runners[stage].calls), 1, f"{stage} should run exactly once")
        self.assertTrue((self.dir / "report.md").is_file())
        self.assertIn("PROMOTION-READY", (self.dir / "report.md").read_text(encoding="utf-8"))

    def test_fresh_incident_reaches_promoted_in_draft_pr_mode(self):
        runners = self._runners(_promotion_dict(mode="draft-pr", pr_status="created"))
        outcome = mod.run_incident(self.dir, stage_runners=runners)

        self.assertEqual(outcome["terminalState"], "PROMOTED")
        report = (self.dir / "report.md").read_text(encoding="utf-8")
        self.assertIn("PROMOTED", report)
        self.assertIn("https://github.com/example/colosseum/pull/1", report)

    def test_stage_files_are_written_with_expected_shapes(self):
        runners = self._runners(_promotion_dict(mode="patch-only"))
        mod.run_incident(self.dir, stage_runners=runners)

        triage_on_disk = json.loads((self.dir / "triage.json").read_text(encoding="utf-8"))
        self.assertEqual(triage_on_disk["verdict"], "CONFIRMED")
        repair_on_disk = json.loads((self.dir / "repair.json").read_text(encoding="utf-8"))
        self.assertTrue(repair_on_disk["accepted"])
        verdict_on_disk = json.loads((self.dir / "verdict.json").read_text(encoding="utf-8"))
        self.assertEqual(verdict_on_disk["decision"], "APPROVE")

    def test_prior_context_accumulates_across_stages(self):
        """Each stage_runner receives `prior` naming every stage already completed - the
        uniform calling contract this module documents (repair sees triage+diagnosis;
        verify additionally sees repair; promotion sees all four)."""
        runners = self._runners(_promotion_dict(mode="patch-only"))
        mod.run_incident(self.dir, stage_runners=runners)

        self.assertEqual(runners["triage"].calls[0]["prior"], [])
        self.assertEqual(runners["diagnosis"].calls[0]["prior"], ["triage"])
        self.assertEqual(runners["repair"].calls[0]["prior"], ["diagnosis", "triage"])
        self.assertEqual(runners["verify"].calls[0]["prior"], ["diagnosis", "repair", "triage"])
        self.assertEqual(
            runners["promotion"].calls[0]["prior"], ["diagnosis", "repair", "triage", "verify"]
        )


# ══════════════════════════════════════════════════════════════════════════
# Terminal-state triggers, each isolated
# ══════════════════════════════════════════════════════════════════════════


class TerminalStateTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9term_")
        self.dir = Path(self._tmp.name)
        _write_incident(self.dir)

    def tearDown(self):
        self._tmp.cleanup()

    def test_flaky_triage_dismisses_without_touching_later_stages(self):
        runners = {
            "triage": _Spy(_triage_dict(verdict="FLAKY", reproduced=True)),
            "diagnosis": _never_called("diagnosis"),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "TRIAGE-DISMISSED")
        self.assertEqual(outcome["ranStages"], ["triage"])

    def test_infra_triage_dismisses(self):
        runners = {
            "triage": _Spy(_triage_dict(verdict="INFRA", reproduced=True)),
            "diagnosis": _never_called("diagnosis"),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "TRIAGE-DISMISSED")

    def test_would_need_forbidden_change_escalates_before_repair(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict(would_need_forbidden=True, escalate=True, may_proceed=False)),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "ESCALATE")
        self.assertEqual(outcome["ranStages"], ["triage", "diagnosis"])

    def test_low_confidence_below_floor_escalates(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict(confidence="low", may_proceed=False)),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "ESCALATE")

    def test_ordinary_repair_exhaustion_is_budget(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict(
                accepted=False,
                escalate_reason="exhausted policy.maxRepairAttempts=3 without an accepted patch; "
                                "last rejection: no test added under tests/",
            )),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "BUDGET")
        self.assertEqual(outcome["ranStages"], ["triage", "diagnosis", "repair"])

    def test_oversized_patch_repair_exhaustion_is_escalate(self):
        """Amendment A8: an oversized patch is named explicitly as its own ESCALATE
        trigger, distinct from ordinary attempt exhaustion (BUDGET)."""
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict(
                accepted=False,
                escalate_reason="exhausted policy.maxRepairAttempts=3 without an accepted patch; "
                                "last rejection: REJECTED (amendment A8 - oversized patches "
                                "escalate instead of promoting): patch_line_count=900 exceeds "
                                "policy.maxPatchLines=400",
            )),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "ESCALATE")

    def test_verifier_reject_escalates(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict()),
            "verify": _Spy(_verdict_dict(decision="REJECT", reasons=["bug test meaningfulness in doubt"])),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "ESCALATE")
        self.assertIn("bug test meaningfulness in doubt", outcome["detail"])

    def test_draft_pr_bridge_blocked_still_reaches_promoted_honestly(self):
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict()),
            "verify": _Spy(_verdict_dict()),
            "promotion": _Spy(_promotion_dict(mode="draft-pr", pr_status="Bridge blocked")),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "PROMOTED")
        self.assertIn("Bridge blocked", outcome["detail"])
        report = (self.dir / "report.md").read_text(encoding="utf-8")
        self.assertIn("Bridge blocked", report)

    def test_sandbox_drift_violation_is_violation_and_stops_immediately(self):
        def _tripwire(incident, incident_dir, policy_obj, *, prior):
            raise mod.sandbox.DriftViolation("MAIN repo drifted between snapshots")

        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _tripwire,
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "VIOLATION")
        self.assertIn("drifted", outcome["detail"])
        # the stage that raised never gets an on-disk stage file.
        self.assertFalse((self.dir / "diagnosis.json").exists())

    def test_stage_wall_clock_exceeded_is_budget(self):
        clock_state = {"t": 0.0}

        def _clock() -> float:
            return clock_state["t"]

        def _slow_repair(incident, incident_dir, policy_obj, *, prior):
            clock_state["t"] += 999_999.0  # blow past policy.perStageTimeoutSec["repair"]
            return _repair_dict()

        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _slow_repair,
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners, clock=_clock)
        self.assertEqual(outcome["terminalState"], "BUDGET")
        self.assertIn("perStageTimeoutSec", outcome["detail"])
        self.assertIn("'repair'", outcome["detail"])

    def test_incident_total_wall_clock_exceeded_is_budget(self):
        """A fake policy with generous per-stage caps but a tight incident-total cap -
        proves the INCIDENT-total accounting fires on its own, distinct from any single
        stage's own cap (the real shipped policy.json's per-stage caps sum to well under
        its own perIncidentTotalSec, so this property needs its own numbers to isolate)."""
        clock_state = {"t": 0.0}

        def _clock() -> float:
            return clock_state["t"]

        def _slow_verify(incident, incident_dir, policy_obj, *, prior):
            clock_state["t"] += 1_500.0  # well under this fake policy's 100000s stage cap
            return _verdict_dict()

        loose_stage_caps_tight_total = _FakePolicy(
            per_stage={stage: 100_000 for stage in mod.LOOP_STAGES},
            per_incident=1_000,
        )
        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict()),
            "verify": _slow_verify,
            "promotion": _never_called("promotion"),
        }
        outcome = mod.run_incident(
            self.dir, stage_runners=runners, clock=_clock,
            policy_obj=loose_stage_caps_tight_total,
        )
        self.assertEqual(outcome["terminalState"], "BUDGET")
        self.assertIn("perIncidentTotalSec", outcome["detail"])


# ══════════════════════════════════════════════════════════════════════════
# Resume: an already-written, valid stage file is not re-run
# ══════════════════════════════════════════════════════════════════════════


class ResumeTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9resume_")
        self.dir = Path(self._tmp.name)
        _write_incident(self.dir)

    def tearDown(self):
        self._tmp.cleanup()

    def test_triage_and_diagnosis_present_resumes_at_repair(self):
        _write_json(self.dir / "triage.json", _triage_dict())
        _write_json(self.dir / "diagnosis.json", _diagnosis_dict())

        runners = {
            "triage": _never_called("triage"),
            "diagnosis": _never_called("diagnosis"),
            "repair": _Spy(_repair_dict()),
            "verify": _Spy(_verdict_dict()),
            "promotion": _Spy(_promotion_dict(mode="patch-only")),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertEqual(outcome["terminalState"], "PROMOTION-READY")
        self.assertEqual(outcome["ranStages"], ["repair", "verify", "promotion"])

    def test_every_stage_file_present_resumes_with_nothing_running(self):
        _write_json(self.dir / "triage.json", _triage_dict())
        _write_json(self.dir / "diagnosis.json", _diagnosis_dict())
        _write_json(self.dir / "repair.json", _repair_dict())
        _write_json(self.dir / "verdict.json", _verdict_dict())
        (self.dir / "report.md").write_text(
            "# Guardian Loop incident AR-test-0001 - PROMOTED\n\nAlready done.\n", encoding="utf-8"
        )

        runners = {stage: _never_called(stage) for stage in mod.LOOP_STAGES}
        outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertTrue(outcome["alreadyTerminal"])
        self.assertEqual(outcome["terminalState"], "PROMOTED")
        self.assertEqual(outcome["ranStages"], [])


# ══════════════════════════════════════════════════════════════════════════
# The plan's mandatory negative control (both directions)
# ══════════════════════════════════════════════════════════════════════════


class NegativeControlTests(unittest.TestCase):
    """Exact assertion: with triage.json+diagnosis.json+repair.json+verdict.json(APPROVE)
    present but report.md NOT yet written (a crash right before promotion), deleting
    triage.json and calling run_incident() must call the triage stage_runner again (assert
    the spy's call count is 1, not 0) and must NOT resolve diagnosis/repair/verify/
    promotion from their still-present-but-now-stale files - the cascade re-runs all of
    them too (every one of the five spies' call counts is asserted to be exactly 1).
    Restoring (here: simply calling run_incident() again once report.md already exists
    from the first call) must run nothing at all - every spy's call count stays at 1, not
    2, and the outcome reports alreadyTerminal=True."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9negctrl_")
        self.dir = Path(self._tmp.name)
        _write_incident(self.dir)
        _write_json(self.dir / "triage.json", _triage_dict())
        _write_json(self.dir / "diagnosis.json", _diagnosis_dict())
        _write_json(self.dir / "repair.json", _repair_dict())
        _write_json(self.dir / "verdict.json", _verdict_dict(decision="APPROVE"))
        # report.md deliberately NOT written yet - mid-sequence, not terminal.

    def tearDown(self):
        self._tmp.cleanup()

    def test_deleting_triage_json_forces_a_full_rerun_not_a_skip_to_diagnosis(self):
        (self.dir / "triage.json").unlink()

        runners = {
            "triage": _Spy(_triage_dict()),
            "diagnosis": _Spy(_diagnosis_dict()),
            "repair": _Spy(_repair_dict()),
            "verify": _Spy(_verdict_dict()),
            "promotion": _Spy(_promotion_dict(mode="patch-only")),
        }
        outcome = mod.run_incident(self.dir, stage_runners=runners)

        # The exact assertion: triage's OWN stage_runner was called (not skipped), and
        # because it ran, every stage after it in the sequence ran too - the cascade -
        # even though diagnosis.json/repair.json/verdict.json were still present and
        # individually well-formed on disk beforehand.
        for stage in mod.LOOP_STAGES:
            self.assertEqual(
                len(runners[stage].calls), 1,
                f"stage {stage!r} must run exactly once after triage.json was deleted "
                "(resume re-runs the first incomplete stage AND everything after it)",
            )
        self.assertEqual(outcome["ranStages"], ["triage", "diagnosis", "repair", "verify", "promotion"])
        self.assertEqual(outcome["terminalState"], "PROMOTION-READY")
        self.assertTrue((self.dir / "report.md").is_file())

        # Restore direction: report.md now exists (written by the call above) - a second
        # resume call must run NOTHING. Re-seed the exact same spies and assert their call
        # counts stay at exactly 1 (not 2).
        second_outcome = mod.run_incident(self.dir, stage_runners=runners)
        self.assertTrue(second_outcome["alreadyTerminal"])
        self.assertEqual(second_outcome["ranStages"], [])
        for stage in mod.LOOP_STAGES:
            self.assertEqual(
                len(runners[stage].calls), 1,
                f"stage {stage!r} must NOT run again once report.md already exists "
                "(the restore direction: resume runs nothing)",
            )


# ══════════════════════════════════════════════════════════════════════════
# render_report() - Hemanth-language, marker-bearing, no emoji
# ══════════════════════════════════════════════════════════════════════════


class ReportRenderingTests(unittest.TestCase):
    def _incident(self):
        return _incident_dict()

    def test_every_terminal_state_renders_its_own_marker_and_no_emoji(self):
        emoji_re = mod._EMOJI_RE
        for state in mod.TERMINAL_STATES:
            body = mod.render_report(
                self._incident(), state, "some detail text.", {},
                generated_at=datetime(2026, 8, 14, 12, 0, tzinfo=timezone.utc),
            )
            self.assertIn(state, body, f"report for {state} must contain its own marker")
            self.assertEqual(emoji_re.findall(body), [], f"report for {state} must carry no emoji")
            self.assertIn("AR-test-0001", body)
            self.assertGreater(len(body), 80, "report body should be non-trivial prose")

    def test_stage_results_are_listed_by_name(self):
        body = mod.render_report(
            self._incident(), "ESCALATE", "diagnosis escalated.",
            {"triage": _triage_dict(), "diagnosis": _diagnosis_dict()},
            generated_at=datetime.now(timezone.utc),
        )
        self.assertIn("triage.json", body)
        self.assertIn("diagnosis.json", body)
        self.assertNotIn("repair.json", body)

    def test_unknown_terminal_state_is_refused(self):
        with self.assertRaises(mod.OrchestratorError):
            mod.render_report(self._incident(), "NOT-A-REAL-STATE", "x", {}, generated_at=datetime.now(timezone.utc))


# ══════════════════════════════════════════════════════════════════════════
# _execute_stage() - budget accounting, pure given a fake clock
# ══════════════════════════════════════════════════════════════════════════


class BudgetAccountingTests(unittest.TestCase):
    def setUp(self):
        self.policy = _FakePolicy(
            per_stage={"triage": 100, "diagnosis": 100, "repair": 100, "verify": 100, "promotion": 100},
            per_incident=1000,
        )

    def test_stage_under_cap_passes_through(self):
        clock_state = {"t": 0.0}
        result = mod._execute_stage(
            "triage", lambda: {"ok": True},
            clock=lambda: clock_state["t"], total_start=0.0,
            policy_obj=self.policy, incident_id="AR-x",
        )
        self.assertEqual(result, {"ok": True})

    def test_stage_over_cap_raises_stage_budget_exceeded(self):
        clock_state = {"t": 0.0}

        def _slow():
            clock_state["t"] += 500.0
            return {"ok": True}

        with self.assertRaises(mod.StageBudgetExceeded):
            mod._execute_stage(
                "triage", _slow,
                clock=lambda: clock_state["t"], total_start=0.0,
                policy_obj=self.policy, incident_id="AR-x",
            )

    def test_incident_total_cap_raises_even_when_stage_itself_is_under_cap(self):
        # total_start=0.0; clock already at 950 (accumulated by earlier stages this run) -
        # this one stage only advances by 60 (well under its own 100s cap), but the
        # RUNNING TOTAL since total_start crosses the 1000s incident cap.
        clock_state = {"t": 950.0}

        def _fast_but_late():
            clock_state["t"] += 60.0
            return {"ok": True}

        with self.assertRaises(mod.IncidentBudgetExceeded):
            mod._execute_stage(
                "triage", _fast_but_late,
                clock=lambda: clock_state["t"], total_start=0.0,
                policy_obj=self.policy, incident_id="AR-x",
            )


class _FakePolicy:
    """A minimal stand-in for policy.Policy carrying only the two fields
    _execute_stage()/_run_loop() actually read - avoids coupling these pure tests to the
    real shipped docs/autorepair/policy.json values."""

    def __init__(self, *, per_stage, per_incident, min_confidence="medium"):
        self.policy = {
            "perStageTimeoutSec": per_stage,
            "perIncidentTotalSec": per_incident,
            "minConfidenceToRepair": min_confidence,
        }


# ══════════════════════════════════════════════════════════════════════════
# owner.lock - unit level
# ══════════════════════════════════════════════════════════════════════════


class LockTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9lock_")
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_lock_round_trips_pid_path_created_at(self):
        lock = mod.acquire_lock(self.dir, pid=4242, now=datetime(2026, 8, 14, tzinfo=timezone.utc))
        on_disk = json.loads((self.dir / mod.LOCK_FILE_NAME).read_text(encoding="utf-8"))
        self.assertEqual(on_disk["pid"], 4242)
        self.assertEqual(on_disk["path"], str(self.dir.resolve()))
        self.assertEqual(on_disk["createdAt"], "2026-08-14T00:00:00+00:00")
        self.assertEqual(lock.pid, 4242)

    def test_second_acquire_against_a_real_live_process_is_refused(self):
        proc = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            self.assertTrue(mod._pid_is_alive(proc.pid), "the spawned subprocess must be alive")
            lock_path = self.dir / mod.LOCK_FILE_NAME
            lock_path.write_text(
                json.dumps({"pid": proc.pid, "path": str(self.dir), "createdAt": "2026-08-14T00:00:00+00:00"}),
                encoding="utf-8",
            )
            with self.assertRaises(mod.LockHeldError):
                mod.acquire_lock(self.dir, pid=os.getpid())
        finally:
            proc.kill()
            proc.wait(timeout=15)

    def test_dead_pid_is_silently_reclaimed(self):
        proc = subprocess.Popen(
            [sys.executable, "-c", "pass"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        dead_pid = proc.pid
        proc.wait(timeout=15)
        # By the time Popen.wait() returns, the process has already exited - its pid is
        # free (barring pid reuse in the tiny window since, vanishingly unlikely here).
        self.assertFalse(mod._pid_is_alive(dead_pid), "the already-exited subprocess must be dead")

        lock_path = self.dir / mod.LOCK_FILE_NAME
        lock_path.write_text(
            json.dumps({"pid": dead_pid, "path": str(self.dir), "createdAt": "2020-01-01T00:00:00+00:00"}),
            encoding="utf-8",
        )
        lock = mod.acquire_lock(self.dir, pid=os.getpid())
        self.assertEqual(lock.pid, os.getpid())
        on_disk = json.loads(lock_path.read_text(encoding="utf-8"))
        self.assertEqual(on_disk["pid"], os.getpid())

    def test_release_only_removes_a_lock_it_still_owns(self):
        lock = mod.acquire_lock(self.dir, pid=111)
        # Simulate another process reclaiming the lock after a stale-pid situation.
        lock_path = self.dir / mod.LOCK_FILE_NAME
        lock_path.write_text(
            json.dumps({"pid": 222, "path": str(self.dir), "createdAt": "2026-08-14T00:00:00+00:00"}),
            encoding="utf-8",
        )
        mod.release_lock(self.dir, lock)  # lock.pid=111, on-disk pid=222 - must NOT delete
        self.assertTrue(lock_path.is_file(), "release_lock() must not delete a lock it no longer owns")

    def test_release_removes_its_own_lock(self):
        lock = mod.acquire_lock(self.dir, pid=333)
        mod.release_lock(self.dir, lock)
        self.assertFalse((self.dir / mod.LOCK_FILE_NAME).exists())


# ══════════════════════════════════════════════════════════════════════════
# owner.lock - integration with run_incident()
# ══════════════════════════════════════════════════════════════════════════


class LockIntegrationTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="g9lockint_")
        self.dir = Path(self._tmp.name)
        _write_incident(self.dir)

    def tearDown(self):
        self._tmp.cleanup()

    def test_run_incident_refuses_while_a_live_process_holds_the_lock(self):
        proc = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            lock_path = self.dir / mod.LOCK_FILE_NAME
            lock_path.write_text(
                json.dumps({"pid": proc.pid, "path": str(self.dir), "createdAt": "2026-08-14T00:00:00+00:00"}),
                encoding="utf-8",
            )
            runners = {stage: _never_called(stage) for stage in mod.LOOP_STAGES}
            with self.assertRaises(mod.LockHeldError):
                mod.run_incident(self.dir, stage_runners=runners)
        finally:
            proc.kill()
            proc.wait(timeout=15)

    def test_run_incident_releases_its_own_lock_after_a_normal_run(self):
        runners = {
            "triage": _Spy(_triage_dict(verdict="FLAKY")),
            "diagnosis": _never_called("diagnosis"),
            "repair": _never_called("repair"),
            "verify": _never_called("verify"),
            "promotion": _never_called("promotion"),
        }
        mod.run_incident(self.dir, stage_runners=runners)
        self.assertFalse((self.dir / mod.LOCK_FILE_NAME).exists(), "the lock must be released after a run")


# ══════════════════════════════════════════════════════════════════════════
# The deferred boundary - default stage runners raise loudly, never silently
# ══════════════════════════════════════════════════════════════════════════


class DeferredStageRunnerTests(unittest.TestCase):
    def test_every_default_stage_runner_raises_not_implemented(self):
        for stage in mod.LOOP_STAGES:
            with self.assertRaises(NotImplementedError):
                mod.DEFAULT_STAGE_RUNNERS[stage]({"id": "AR-x"}, Path("."), object(), prior={})

    def test_running_a_fresh_incident_with_no_injected_runners_raises_not_implemented(self):
        with tempfile.TemporaryDirectory(prefix="g9deferred_") as tmp:
            incident_dir = Path(tmp)
            _write_incident(incident_dir)
            with self.assertRaises(NotImplementedError):
                mod.run_incident(incident_dir)


# ══════════════════════════════════════════════════════════════════════════
# CLI argument validation
# ══════════════════════════════════════════════════════════════════════════


class CliArgumentTests(unittest.TestCase):
    def test_incident_without_resume_flag_errors(self):
        with self.assertRaises(SystemExit):
            mod.main(["--incident", "AR-2026-08-14-0001"])

    def test_resume_on_missing_incident_dir_errors(self):
        with tempfile.TemporaryDirectory(prefix="g9cli_") as tmp:
            with self.assertRaises(SystemExit):
                mod.main([
                    "--incident", "AR-does-not-exist", "--resume",
                    "--artifacts-root", tmp,
                ])


if __name__ == "__main__":
    print(f"[test_autorepair_orchestrator] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)

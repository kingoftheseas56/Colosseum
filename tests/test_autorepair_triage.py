#!/usr/bin/env python3
"""test_autorepair_triage.py - tests for scripts/autorepair/triage.py and
scripts/autorepair/hooks/guard.py (Guardian Loop Slice G4: "Triage - reproduce or
dismiss, and the headless-agent probe").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G4, decision D4
(triage is CODE not a model), and binding pressure-test amendment A2 (path
canonicalization + egress-binary denial for the guard hook). Pure Python, stdlib
unittest, house flat convention (D8: tests/test_autorepair_*.py) - runnable directly:

    python tests/test_autorepair_triage.py -v

HERMETIC end to end: no sandbox is built, no `claude -p` session runs, no network call is
made. The classifier tests feed classify_triage() canned RunResults by hand (D4's own
mandate: triage is a script that counts, so its tests are exactly that - no live
reproduce, no live headless probe; both are the DEFERRED batched-runtime work this slice
names but does not perform). The guard-hook tests exercise decide() directly against real
temp-directory fixtures, including a REAL NTFS junction created via `mklink /J` (proven
live at authoring time: Path.resolve() follows a junction to its real target even for a
not-yet-existing final path component, exactly the escape amendment A2 calls out).

Test groups:
  TriageClassifierTests           - D4's verdict math on canned run-result sets: 3/3 fail
                                     at the same step -> CONFIRMED; 1/3 fail -> FLAKY;
                                     a boot/isolation failure before the asserted step ->
                                     INFRA; 2/3 fail at the same step with
                                     confirmThreshold=2 -> CONFIRMED.
  TriageNegativeControlTests      - the plan's mandatory negative control, both
                                     directions: failures land at DIFFERENT steps ->
                                     FLAKY, not CONFIRMED (consistency matters, not just
                                     failure count); making the same run set consistent ->
                                     CONFIRMED.
  TriageValidationTests           - classify_triage() fails closed on a threshold that
                                     exceeds runs, and on a run-result count that does not
                                     match policy.triage.runs exactly.
  TriageOrchestrationSeamTests    - triage()'s injectable run_once seam: an injected
                                     canned callable drives a full classification without
                                     ever touching a sandbox; the DEFAULT run_once
                                     (default_run_once) raises NotImplementedError rather
                                     than silently doing live work - the deferred boundary
                                     proven loud, not silent.
  GuardHookDecideTests            - decide()'s A2 containment + egress rules directly: an
                                     in-sandbox read allowed; an outside read, a '..'
                                     escape, and a real junction escape all denied; curl/
                                     git push/pip denied; an ordinary in-sandbox `git
                                     diff` and file Edit allowed; a %USERPROFILE% absolute
                                     read denied; fail-closed when the sandbox root is
                                     unset (None and empty string both).
  GuardHookCliTests                - the stdin/stdout CLI wrapper's JSON contract, via a
                                     real subprocess invocation (allow and deny cases,
                                     plus malformed-stdin fail-closed).
"""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TRIAGE_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "triage.py"
GUARD_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "hooks" / "guard.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


triage_mod = _load_module("autorepair_triage", TRIAGE_SCRIPT_PATH)
guard_mod = _load_module("autorepair_guard", GUARD_SCRIPT_PATH)

RunResult = triage_mod.RunResult
classify_triage = triage_mod.classify_triage


# ══════════════════════════════════════════════════════════════════════════
# classify_triage() - D4's verdict math
# ══════════════════════════════════════════════════════════════════════════


class TriageClassifierTests(unittest.TestCase):
    def test_three_of_three_fail_same_step_is_confirmed(self):
        runs = [RunResult(status="FAIL", stepLabel="step-A") for _ in range(3)]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(result["verdict"], "CONFIRMED")
        self.assertEqual(result["failingStepConsistency"]["label"], "step-A")
        self.assertEqual(result["failingStepConsistency"]["count"], 3)
        self.assertEqual(len(result["runs"]), 3)

    def test_one_of_three_fail_is_flaky(self):
        runs = [
            RunResult(status="FAIL", stepLabel="step-A"),
            RunResult(status="PASS"),
            RunResult(status="PASS"),
        ]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(result["verdict"], "FLAKY")
        self.assertEqual(result["failingStepConsistency"]["count"], 1)

    def test_boot_isolation_failure_before_step_is_infra(self):
        runs = [
            RunResult(status="INFRA", stepLabel="session boot"),
            RunResult(status="PASS"),
            RunResult(status="PASS"),
        ]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(result["verdict"], "INFRA")
        self.assertFalse(result["failingStepConsistency"]["applicable"])
        self.assertIn("session boot", result["failingStepConsistency"]["infraStepLabels"])

    def test_infra_takes_precedence_even_when_other_runs_fail(self):
        """A mixed set (one INFRA, one genuine FAIL at the incident's own step) must
        still resolve to INFRA, not FLAKY or CONFIRMED - a broken sandbox/environment on
        ANY of the k runs is not evidence about the bug's flakiness (interpretation call
        flagged in the G4 report: INFRA pre-empts FAIL/PASS counting entirely)."""
        runs = [
            RunResult(status="INFRA", stepLabel="session boot"),
            RunResult(status="FAIL", stepLabel="step-A"),
            RunResult(status="FAIL", stepLabel="step-A"),
        ]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(result["verdict"], "INFRA")

    def test_two_of_three_fail_same_step_threshold_two_is_confirmed(self):
        runs = [
            RunResult(status="FAIL", stepLabel="step-A"),
            RunResult(status="FAIL", stepLabel="step-A"),
            RunResult(status="PASS"),
        ]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(result["verdict"], "CONFIRMED")
        self.assertEqual(result["failingStepConsistency"]["count"], 2)
        self.assertEqual(result["failingStepConsistency"]["ofFailures"], 2)


# ══════════════════════════════════════════════════════════════════════════
# Negative control (mandatory, both directions): step CONSISTENCY, not just count
# ══════════════════════════════════════════════════════════════════════════


class TriageNegativeControlTests(unittest.TestCase):
    """The plan's mandatory negative control: 'a run set where the failing STEP differs
    across the failures -> FLAKY, NOT CONFIRMED (consistency matters, not just failure
    count); restore -> CONFIRMED.' Same failure COUNT (3 of 3) in both directions - only
    the step-label agreement changes - to isolate exactly the property D4 requires."""

    def test_different_failing_steps_are_flaky_not_confirmed(self):
        runs = [
            RunResult(status="FAIL", stepLabel="step-A"),
            RunResult(status="FAIL", stepLabel="step-B"),
            RunResult(status="FAIL", stepLabel="step-C"),
        ]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        # The exact assertion: 3 of 3 runs FAILED, yet the verdict is FLAKY because no
        # single step's count reaches confirmThreshold=2 - failure COUNT alone (3) would
        # wrongly suggest CONFIRMED; only step-label consistency actually decides it.
        self.assertEqual(
            result["verdict"], "FLAKY",
            "3/3 failures at three DIFFERENT steps must be FLAKY, not CONFIRMED - "
            "consistency of the failing step matters, not raw failure count",
        )
        self.assertEqual(result["failingStepConsistency"]["count"], 1)
        self.assertEqual(result["failingStepConsistency"]["distinctFailingSteps"], 3)

    def test_making_the_same_run_set_consistent_flips_to_confirmed(self):
        """The restore direction: identical failure count (3/3), same confirmThreshold -
        only the step labels are made consistent, and the verdict flips to CONFIRMED."""
        runs = [RunResult(status="FAIL", stepLabel="step-A") for _ in range(3)]
        result = classify_triage(runs, runs=3, confirm_threshold=2)
        self.assertEqual(
            result["verdict"], "CONFIRMED",
            "the SAME 3/3 failure count, now all at one step, must flip to CONFIRMED",
        )
        self.assertEqual(result["failingStepConsistency"]["distinctFailingSteps"], 1)


# ══════════════════════════════════════════════════════════════════════════
# classify_triage() input validation - fails closed, never guesses
# ══════════════════════════════════════════════════════════════════════════


class TriageValidationTests(unittest.TestCase):
    def test_threshold_exceeding_runs_refuses(self):
        runs = [RunResult(status="PASS") for _ in range(3)]
        with self.assertRaises(triage_mod.TriageError):
            classify_triage(runs, runs=3, confirm_threshold=5)

    def test_short_run_set_refuses(self):
        runs = [RunResult(status="FAIL", stepLabel="step-A") for _ in range(2)]
        with self.assertRaises(triage_mod.TriageError):
            classify_triage(runs, runs=3, confirm_threshold=2)

    def test_run_result_rejects_fail_with_no_step_label(self):
        with self.assertRaises(triage_mod.TriageError):
            RunResult(status="FAIL", stepLabel=None)

    def test_run_result_rejects_pass_with_a_step_label(self):
        with self.assertRaises(triage_mod.TriageError):
            RunResult(status="PASS", stepLabel="step-A")

    def test_run_result_rejects_unknown_status(self):
        with self.assertRaises(triage_mod.TriageError):
            RunResult(status="SKIPPED", stepLabel="step-A")


# ══════════════════════════════════════════════════════════════════════════
# triage(): the orchestration seam - injectable run_once, deferred default
# ══════════════════════════════════════════════════════════════════════════


class TriageOrchestrationSeamTests(unittest.TestCase):
    def test_injected_run_once_drives_a_full_classification(self):
        incident = {"id": "AR-test-0001", "baseSha": "deadbeef"}
        canned = iter([RunResult(status="FAIL", stepLabel="step-A")] * 3)

        calls: list[int] = []

        def fake_run_once(index, inc):
            self.assertIs(inc, incident, "run_once must receive the same incident object")
            calls.append(index)
            return next(canned)

        # Real shipped policy (docs/autorepair/policy.json: triage.runs=3,
        # confirmThreshold=2) - triage() with no policy_obj override only READS that
        # JSON via load_policy(), it never touches a sandbox, so this is safe here.
        result = triage_mod.triage(incident, run_once=fake_run_once)

        self.assertEqual(calls, [0, 1, 2], "run_once must be called once per policy.triage.runs")
        self.assertEqual(result["verdict"], "CONFIRMED")
        self.assertEqual(result["incidentId"], "AR-test-0001")
        self.assertTrue(result["reproduced"])

    def test_default_run_once_is_deferred_and_raises_loudly(self):
        """The DEFERRED boundary must be loud, not silent: calling the real (unwired)
        live runner raises NotImplementedError rather than fabricating a RunResult."""
        incident = {"id": "AR-test-0002"}
        with self.assertRaises(NotImplementedError):
            triage_mod.default_run_once(0, incident)

    def test_make_live_run_once_factory_is_also_deferred(self):
        incident = {"id": "AR-test-0003", "baseSha": "deadbeef"}
        with self.assertRaises(NotImplementedError):
            triage_mod.make_live_run_once(incident)


# ══════════════════════════════════════════════════════════════════════════
# guard.py decide() - A2 containment + egress denial
# ══════════════════════════════════════════════════════════════════════════


class GuardHookDecideTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g4guard_")
        self.tmp_path = Path(self._tmp.name)
        self.sandbox = self.tmp_path / "sandbox"
        (self.sandbox / "native" / "engine").mkdir(parents=True)
        (self.sandbox / "native" / "engine" / "Foo.cpp").write_text("// v1\n", encoding="utf-8")
        self.outside = self.tmp_path / "outside"
        self.outside.mkdir()
        (self.outside / "secret.txt").write_text("top secret\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_in_sandbox_read_is_allowed(self):
        result = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": str(self.sandbox / "native" / "engine" / "Foo.cpp")}},
            sandbox_root=self.sandbox,
        )
        self.assertTrue(result["allow"], result["reason"])

    def test_read_outside_sandbox_is_denied(self):
        result = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": str(self.outside / "secret.txt")}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_dotdot_escape_is_denied(self):
        escape_path = str(self.sandbox) + "\\..\\outside\\secret.txt"
        result = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": escape_path}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])
        self.assertIn("outside", result["reason"])

    def test_junction_escape_is_denied(self):
        """A2's own named requirement: 'Junction/reparse/.lnk escapes must be caught by
        canonicalization (add a test case).' A REAL NTFS junction is created here
        (mklink /J - unprivileged, proven live at authoring time) from inside the
        sandbox to the outside fixture dir; canonicalize() must resolve through it and
        deny the escape, not be fooled by the textually-inside-looking raw path."""
        junction = self.sandbox / "escape"
        result = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(junction), str(self.outside)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, f"mklink /J failed: {result.stderr}")

        decision = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": str(junction / "secret.txt")}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(
            decision["allow"],
            "a junction planted inside the sandbox that resolves outside it must still "
            "be denied - canonicalization, not raw-text containment",
        )

    def test_curl_command_is_denied(self):
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "curl https://example.com/payload"}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_git_push_command_is_denied(self):
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "git push origin autorepair/AR-0001"}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_pip_command_is_denied(self):
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "pip install requests"}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_git_gc_command_is_denied(self):
        """A1: never gc/repack/prune a --no-hardlinks sandbox clone."""
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "git gc --aggressive"}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_ordinary_git_diff_is_allowed(self):
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "git diff -- native/engine/Foo.cpp"}},
            sandbox_root=self.sandbox,
        )
        self.assertTrue(result["allow"], result["reason"])

    def test_ordinary_in_sandbox_edit_is_allowed(self):
        result = guard_mod.decide(
            {
                "tool_name": "Edit",
                "tool_input": {
                    "file_path": str(self.sandbox / "native" / "engine" / "Foo.cpp"),
                    "old_string": "// v1",
                    "new_string": "// v2",
                },
            },
            sandbox_root=self.sandbox,
        )
        self.assertTrue(result["allow"], result["reason"])

    def test_userprofile_absolute_read_is_denied(self):
        result = guard_mod.decide(
            {"tool_name": "Bash", "tool_input": {"command": "type %USERPROFILE%\\Desktop\\secret.txt"}},
            sandbox_root=self.sandbox,
        )
        self.assertFalse(result["allow"])

    def test_fail_closed_when_sandbox_root_is_none(self):
        result = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": str(self.sandbox / "native" / "engine" / "Foo.cpp")}},
            sandbox_root=None,
        )
        self.assertFalse(result["allow"])
        self.assertIn("failing closed", result["reason"])

    def test_fail_closed_when_sandbox_root_is_empty_string(self):
        result = guard_mod.decide(
            {"tool_name": "Read", "tool_input": {"file_path": str(self.sandbox / "native" / "engine" / "Foo.cpp")}},
            sandbox_root="",
        )
        self.assertFalse(result["allow"])

    def test_unlisted_tool_is_out_of_scope_and_allowed(self):
        """A tool this hook does not model (e.g. TodoWrite) is allowed BY THIS HOOK -
        what a headless stage may call at all is D3's separate --allowedTools whitelist,
        set at invocation time, not this hook's job."""
        result = guard_mod.decide(
            {"tool_name": "TodoWrite", "tool_input": {"todos": []}},
            sandbox_root=self.sandbox,
        )
        self.assertTrue(result["allow"])


# ══════════════════════════════════════════════════════════════════════════
# guard.py CLI - the stdin/stdout PreToolUse hook wrapper's JSON contract
# ══════════════════════════════════════════════════════════════════════════


class GuardHookCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g4guardcli_")
        self.sandbox = Path(self._tmp.name) / "sandbox"
        self.sandbox.mkdir()
        (self.sandbox / "in-sandbox.txt").write_text("x\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _run_cli(self, payload: dict, sandbox_root: str) -> dict:
        proc = subprocess.run(
            [sys.executable, str(GUARD_SCRIPT_PATH), "--sandbox-root", sandbox_root],
            input=json.dumps(payload), capture_output=True, text=True,
        )
        self.assertEqual(proc.returncode, 0, f"guard CLI must always exit 0: stderr={proc.stderr}")
        return json.loads(proc.stdout)

    def test_cli_allow_shape(self):
        out = self._run_cli(
            {"tool_name": "Read", "tool_input": {"file_path": str(self.sandbox / "in-sandbox.txt")}},
            str(self.sandbox),
        )
        self.assertEqual(out["decision"], "approve")
        self.assertEqual(out["hookSpecificOutput"]["permissionDecision"], "allow")
        self.assertEqual(out["hookSpecificOutput"]["hookEventName"], "PreToolUse")

    def test_cli_deny_shape(self):
        out = self._run_cli(
            {"tool_name": "Bash", "tool_input": {"command": "curl http://example.com"}},
            str(self.sandbox),
        )
        self.assertEqual(out["decision"], "block")
        self.assertEqual(out["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_cli_fails_closed_on_malformed_stdin(self):
        proc = subprocess.run(
            [sys.executable, str(GUARD_SCRIPT_PATH), "--sandbox-root", str(self.sandbox)],
            input="not json", capture_output=True, text=True,
        )
        self.assertEqual(proc.returncode, 0)
        out = json.loads(proc.stdout)
        self.assertEqual(out["decision"], "block")


if __name__ == "__main__":
    print(f"[test_autorepair_triage] scripts under test: {TRIAGE_SCRIPT_PATH}, {GUARD_SCRIPT_PATH}")
    unittest.main(verbosity=2)

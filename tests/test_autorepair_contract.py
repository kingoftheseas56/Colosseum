#!/usr/bin/env python3
"""test_autorepair_contract.py - tests for scripts/autorepair/repair_contract.py (Guardian
Loop Slice G6: "Repair - handcuffed edits, mandatory bug test, mechanical red/green").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G6. Pure Python,
stdlib unittest, house flat convention (D8: tests/test_autorepair_*.py). Runnable directly:

    python tests/test_autorepair_contract.py -v

HERMETIC end to end: no agent runs, no compiler runs, no `claude -p` session runs anywhere
in this file. The three pure gates (validate_patch_shape, validate_bugtest_command,
evaluate_red_green) are exercised on hand-built canned dicts/lists - no I/O at all.
count_patch_lines() and the run_repair() orchestration seam ARE exercised against a real
throwaway git fixture repo (mirrors tests/test_autorepair_sandbox.py's own pattern - real
git plumbing against a fixture repo is not "an agent or a build," it is the same kind of
hermetic mechanical operation sandbox.py's own tests already rely on) - never the real
Colosseum repo, never a real compiler, never a real headless-Sonnet call. The one real
deferred boundary (default_invoke()) is asserted to raise NotImplementedError, never called
for real.

Test groups:
  PatchShapeTests                 - a clean patch (1+ testAdd, 0 forbidden, within
                                     maxPatchLines) accepts; a patch with a modified
                                     existing test, a patch touching
                                     docs/autorepair/policy.json, a patch with no test
                                     added, and an oversized patch each reject, one
                                     violation at a time.
  BugtestCommandTests              - A4's template constraint: `ctest -R <name>` whose
                                     name is the added test's own file stem accepts; a
                                     ctest target NOT among testAdds rejects; a free-form
                                     `bash foo.sh` command rejects; missing/false
                                     expectRedWithoutFix rejects; the `lanista session run
                                     <scenario>` template is exercised too (accept + reject
                                     variants), plus unknown/missing bugtest fields.
  RedGreenTests                    - red=[1,1] green=[0,0] accepts.
  RedGreenNegativeControlTests     - THE MANDATORY NEGATIVE CONTROL, both directions:
                                     red=[0,0] (a bug test that PASSES without the
                                     production fix - vacuous) rejects, naming the
                                     vacuity; a genuine red=[1,1] green=[0,0] accepts.
                                     Also: wrong run counts (1 or 3, either side) reject;
                                     a green run that fails rejects.
  PatchLineCountTests               - count_patch_lines() against a real throwaway git
                                     fixture: a clean two-line addition counts 2; a
                                     multi-file patch sums across files.
  RunRepairOrchestrationSeamTests   - run_repair()'s injectable invoke seam, against a
                                     real throwaway git fixture repo: a clean first-attempt
                                     patch accepts immediately; a rejected first attempt
                                     followed by a clean second attempt accepts on attempt
                                     2 and feeds the first rejection VERBATIM into the
                                     second invoke() call; budget exhaustion (every attempt
                                     rejected) returns accepted=False without raising;
                                     default_invoke() raises NotImplementedError rather
                                     than silently doing live work.
"""
from __future__ import annotations

import dataclasses
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONTRACT_SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "repair_contract.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


contract_mod = _load_module("autorepair_repair_contract", CONTRACT_SCRIPT_PATH)

validate_patch_shape = contract_mod.validate_patch_shape
validate_bugtest_command = contract_mod.validate_bugtest_command
evaluate_red_green = contract_mod.evaluate_red_green
count_patch_lines = contract_mod.count_patch_lines
run_repair = contract_mod.run_repair
RepairContractError = contract_mod.RepairContractError


# ── git plumbing (mirrors tests/test_autorepair_sandbox.py's own _git/_write) ──


def _git(cwd: Path, *args: str) -> subprocess.CompletedProcess:
    result = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True)
    if result.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed in {cwd}: {result.stderr}")
    return result


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def _init_fixture_repo(root: Path) -> None:
    _git(root, "init", "-q")
    _git(root, "config", "user.email", "test@example.com")
    _git(root, "config", "user.name", "Fixture Author")


# ══════════════════════════════════════════════════════════════════════════
# validate_patch_shape() - ruling 2 (forbidden), ruling 5 (bug-test door), A8 (size)
# ══════════════════════════════════════════════════════════════════════════


def _classification(**overrides) -> dict:
    payload = {
        "testAdds": ["tests/test_bug_12345.py"],
        "forbidden": [],
        "production": ["native/engine/Foo.cpp"],
    }
    payload.update(overrides)
    return payload


class PatchShapeTests(unittest.TestCase):
    def test_clean_patch_accepts(self):
        self.assertIsNone(
            validate_patch_shape(_classification(), max_patch_lines=400, patch_line_count=42)
        )

    def test_modified_existing_test_rejects(self):
        classification = _classification(forbidden=["tests/foo.py"])
        with self.assertRaises(RepairContractError) as ctx:
            validate_patch_shape(classification, max_patch_lines=400, patch_line_count=10)
        message = str(ctx.exception)
        self.assertIn("ruling 2", message)
        self.assertIn("tests/foo.py", message)

    def test_modified_policy_json_rejects(self):
        classification = _classification(forbidden=["docs/autorepair/policy.json"])
        with self.assertRaises(RepairContractError) as ctx:
            validate_patch_shape(classification, max_patch_lines=400, patch_line_count=10)
        self.assertIn("docs/autorepair/policy.json", str(ctx.exception))

    def test_no_test_added_rejects(self):
        classification = _classification(testAdds=[])
        with self.assertRaises(RepairContractError) as ctx:
            validate_patch_shape(classification, max_patch_lines=400, patch_line_count=10)
        message = str(ctx.exception)
        self.assertIn("ruling 5", message)
        self.assertIn("bug-test door", message)

    def test_oversized_patch_rejects(self):
        with self.assertRaises(RepairContractError) as ctx:
            validate_patch_shape(_classification(), max_patch_lines=400, patch_line_count=401)
        message = str(ctx.exception)
        self.assertIn("A8", message)
        self.assertIn("401", message)
        self.assertIn("400", message)

    def test_patch_line_count_exactly_at_the_cap_accepts(self):
        """Boundary check: the cap itself is not oversized (only STRICTLY greater than
        max_patch_lines rejects)."""
        self.assertIsNone(
            validate_patch_shape(_classification(), max_patch_lines=400, patch_line_count=400)
        )

    def test_malformed_classification_shape_rejects(self):
        with self.assertRaises(RepairContractError):
            validate_patch_shape({"testAdds": []}, max_patch_lines=400, patch_line_count=1)

    def test_empty_production_rejects(self):
        """F3 hardening (Guardian Loop audit, LOW, cheap): a repair that adds only a
        test and changes no production code proves nothing was actually fixed - a real
        repair must change production code, not only add tests."""
        classification = _classification(production=[])
        with self.assertRaises(RepairContractError) as ctx:
            validate_patch_shape(classification, max_patch_lines=400, patch_line_count=10)
        message = str(ctx.exception)
        self.assertIn("F3", message)
        self.assertIn("production", message.lower())

    def test_nonempty_production_accepts(self):
        """Negative control, direction 2: the same shape restored to a non-empty
        production list accepts (paired with test_empty_production_rejects above)."""
        with self.assertRaises(RepairContractError):
            validate_patch_shape(_classification(production=[]), max_patch_lines=400, patch_line_count=10)
        self.assertIsNone(
            validate_patch_shape(_classification(), max_patch_lines=400, patch_line_count=42)
        )


# ══════════════════════════════════════════════════════════════════════════
# validate_bugtest_command() - A4, template-constrained
# ══════════════════════════════════════════════════════════════════════════


class BugtestCommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.test_adds = ["tests/auto/foo/tst_bug12345.cpp"]

    def test_ctest_target_in_test_adds_accepts(self):
        bugtest = {"cmd": "ctest", "args": ["-R", "tst_bug12345"], "expectRedWithoutFix": True}
        self.assertIsNone(validate_bugtest_command(bugtest, self.test_adds))

    def test_ctest_target_not_in_test_adds_rejects(self):
        bugtest = {"cmd": "ctest", "args": ["-R", "tst_some_other_test"], "expectRedWithoutFix": True}
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        message = str(ctx.exception)
        self.assertIn("tst_some_other_test", message)
        self.assertIn("A4", message)

    def test_free_form_bash_command_rejects(self):
        bugtest = {"cmd": "bash", "args": ["foo.sh"], "expectRedWithoutFix": True}
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("ctest", str(ctx.exception))
        self.assertIn("lanista", str(ctx.exception))

    def test_expect_red_without_fix_missing_rejects(self):
        bugtest = {"cmd": "ctest", "args": ["-R", "tst_bug12345"]}
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("expectRedWithoutFix", str(ctx.exception))

    def test_expect_red_without_fix_false_rejects(self):
        bugtest = {
            "cmd": "ctest",
            "args": ["-R", "tst_bug12345"],
            "expectRedWithoutFix": False,
        }
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("expectRedWithoutFix", str(ctx.exception))

    def test_lanista_scenario_in_test_adds_accepts(self):
        test_adds = ["tests/lanista_scenarios/bug_repro.json"]
        bugtest = {
            "cmd": "lanista",
            "args": ["session", "run", "tests/lanista_scenarios/bug_repro.json"],
            "expectRedWithoutFix": True,
        }
        self.assertIsNone(validate_bugtest_command(bugtest, test_adds))

    def test_lanista_scenario_not_in_test_adds_rejects(self):
        test_adds = ["tests/lanista_scenarios/bug_repro.json"]
        bugtest = {
            "cmd": "lanista",
            "args": ["session", "run", "tests/lanista_scenarios/some_other_scenario.json"],
            "expectRedWithoutFix": True,
        }
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, test_adds)
        self.assertIn("some_other_scenario.json", str(ctx.exception))

    def test_lanista_wrong_args_shape_rejects(self):
        bugtest = {
            "cmd": "lanista",
            "args": ["run", "tests/lanista_scenarios/bug_repro.json"],
            "expectRedWithoutFix": True,
        }
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, ["tests/lanista_scenarios/bug_repro.json"])
        self.assertIn("session run", str(ctx.exception))

    def test_ctest_wrong_args_shape_rejects(self):
        bugtest = {"cmd": "ctest", "args": ["tst_bug12345"], "expectRedWithoutFix": True}
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("-R", str(ctx.exception))

    def test_unknown_bugtest_field_rejects_naming_it(self):
        bugtest = {
            "cmd": "ctest",
            "args": ["-R", "tst_bug12345"],
            "expectRedWithoutFix": True,
            "extra": "nope",
        }
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("extra", str(ctx.exception))

    def test_missing_args_field_rejects_naming_it(self):
        bugtest = {"cmd": "ctest", "expectRedWithoutFix": True}
        with self.assertRaises(RepairContractError) as ctx:
            validate_bugtest_command(bugtest, self.test_adds)
        self.assertIn("args", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# evaluate_red_green() - ruling 5 / A4, mechanical
# ══════════════════════════════════════════════════════════════════════════


class RedGreenTests(unittest.TestCase):
    def test_genuine_red_then_green_accepts(self):
        self.assertIsNone(evaluate_red_green([1, 1], [0, 0]))

    def test_different_nonzero_red_codes_still_accept(self):
        """Red just needs NONZERO, not any specific value - a segfault (139) is as valid
        a red proof as an assertion failure (1)."""
        self.assertIsNone(evaluate_red_green([1, 139], [0, 0]))


class RedGreenNegativeControlTests(unittest.TestCase):
    """The plan's mandatory negative control (Slice G6's own "Focused tests" contract),
    exercised BOTH directions: 'canned patch whose added bug test PASSES even without the
    production fix (a vacuous test) -> the red-check rejects it - the exact class of fake
    proof this program exists to kill; restore a genuine fixture -> accepted.'"""

    def test_vacuous_bug_test_red_all_zero_rejects_naming_the_vacuity(self):
        with self.assertRaises(RepairContractError) as ctx:
            evaluate_red_green([0, 0], [0, 0])
        message = str(ctx.exception)
        self.assertIn("VACUOUS", message)
        self.assertIn("[0, 1]", message)  # both red-run indexes named as vacuous

    def test_vacuous_bug_test_one_red_run_zero_still_rejects(self):
        """Only ONE of the two red runs exiting 0 is still a vacuous proof - A4's '2/2'
        bar means BOTH must be genuinely red, not just the majority."""
        with self.assertRaises(RepairContractError) as ctx:
            evaluate_red_green([1, 0], [0, 0])
        self.assertIn("VACUOUS", str(ctx.exception))

    def test_restored_genuine_fixture_accepts(self):
        """The exact negative-control pairing: the same shape that just rejected on
        red=[0,0], restored to a genuine red=[1,1], now accepts - both directions on one
        assertion path."""
        with self.assertRaises(RepairContractError):
            evaluate_red_green([0, 0], [0, 0])
        self.assertIsNone(evaluate_red_green([1, 1], [0, 0]))

    def test_green_run_that_fails_rejects(self):
        with self.assertRaises(RepairContractError) as ctx:
            evaluate_red_green([1, 1], [0, 1])
        message = str(ctx.exception)
        self.assertIn("GREEN DID NOT PASS", message)
        self.assertIn("[1]", message)

    def test_fewer_than_two_red_runs_rejects(self):
        with self.assertRaises(RepairContractError) as ctx:
            evaluate_red_green([1], [0, 0])
        self.assertIn("A4", str(ctx.exception))

    def test_fewer_than_two_green_runs_rejects(self):
        with self.assertRaises(RepairContractError):
            evaluate_red_green([1, 1], [0])

    def test_more_than_two_runs_rejects(self):
        """A4 pins the shape to EXACTLY 2/2, not 'at least 2' - three lucky red runs is
        still a shape violation, not a stronger proof."""
        with self.assertRaises(RepairContractError):
            evaluate_red_green([1, 1, 1], [0, 0])


# ══════════════════════════════════════════════════════════════════════════
# count_patch_lines() - the one real (mechanical) I/O, against a git fixture
# ══════════════════════════════════════════════════════════════════════════


class PatchLineCountTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g6linecount_")
        self.root = Path(self._tmp.name) / "sandbox"
        self.root.mkdir()
        _init_fixture_repo(self.root)
        _write(self.root / "native" / "engine" / "Foo.cpp", "line1\nline2\n")
        _git(self.root, "add", "-A")
        _git(self.root, "commit", "-q", "-m", "seed")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_single_file_two_line_addition_counts_two(self):
        _write(self.root / "tests" / "test_new_bug.py", "line_a\nline_b\n")
        _git(self.root, "add", "-A")
        self.assertEqual(count_patch_lines(self.root), 2)

    def test_multi_file_patch_sums_across_files(self):
        _write(self.root / "native" / "engine" / "Foo.cpp", "line1\nline2 CHANGED\nline3\n")
        _write(self.root / "tests" / "test_new_bug.py", "line_a\nline_b\nline_c\n")
        _git(self.root, "add", "-A")
        # Foo.cpp: 1 line removed ("line2") + 2 lines added ("line2 CHANGED", "line3") = 3.
        # test_new_bug.py: 3 lines added (new file) = 3. Total = 6.
        self.assertEqual(count_patch_lines(self.root), 6)

    def test_no_staged_changes_counts_zero(self):
        self.assertEqual(count_patch_lines(self.root), 0)


# ══════════════════════════════════════════════════════════════════════════
# run_repair(): the orchestration seam - injectable invoke, deferred default
# ══════════════════════════════════════════════════════════════════════════


class _CannedPolicy:
    """A minimal stand-in for scripts/autorepair/policy.py's Policy dataclass - only the
    three fields run_repair() actually reads (modelRouting.repair, maxRepairAttempts,
    maxPatchLines). Mirrors test_autorepair_diagnosis.py's own _CannedPolicy pattern.
    ONLY safe for tests where invoke() itself never returns (raises before run_repair()
    ever reaches sandbox.extract_patch()) - extract_patch() calls the REAL
    Policy.is_forbidden(), which this stand-in does not implement at all. Tests that let
    an attempt actually reach classification use _policy_with_overrides() below instead."""

    def __init__(self, *, max_repair_attempts: int = 3, max_patch_lines: int = 400):
        self.policy = {
            "modelRouting": {"diagnosis": "opus", "repair": "sonnet", "verify": "opus"},
            "maxRepairAttempts": max_repair_attempts,
            "maxPatchLines": max_patch_lines,
        }


def _policy_with_overrides(*, max_repair_attempts: int = 3, max_patch_lines: int = 400):
    """The REAL, shipped docs/autorepair/ law (real Policy.is_forbidden(), real
    forbidden-paths.json) with only maxRepairAttempts/maxPatchLines overridden - for tests
    where an attempt actually reaches sandbox.extract_patch() (which calls
    policy_obj.is_forbidden() for real classification; a canned stand-in cannot satisfy
    that call). `policy.policy` is a plain, mutable dict on an otherwise-frozen dataclass
    (see policy.py's own Policy definition) - dataclasses.replace() swaps in an overridden
    copy of just that dict, leaving forbidden_modify_delete/forbidden_add_exempt/
    risk_classes (what is_forbidden() actually reads) untouched and real."""
    real_policy = contract_mod.load_policy()
    overridden = {
        **real_policy.policy,
        "maxRepairAttempts": max_repair_attempts,
        "maxPatchLines": max_patch_lines,
    }
    return dataclasses.replace(real_policy, policy=overridden)


class RunRepairOrchestrationSeamTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="g6runrepair_")
        self.sandbox_root = Path(self._tmp.name) / "sandbox"
        self.sandbox_root.mkdir()
        _init_fixture_repo(self.sandbox_root)
        _write(self.sandbox_root / "native" / "engine" / "Foo.cpp", "// v1 - buggy\n")
        _write(self.sandbox_root / "tests" / "foo.py", "# v1 - pre-existing test\n")
        _git(self.sandbox_root, "add", "-A")
        _git(self.sandbox_root, "commit", "-q", "-m", "base incident commit")

        self.incident = {"id": "AR-test-0006", "baseSha": "deadbeef"}
        self.diagnosis = {"rootCause": {"file": "native/engine/Foo.cpp", "line": 1}}

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _reset_sandbox(self) -> None:
        """Stands in for invoke()'s own real-implementation responsibility (documented in
        default_invoke()'s docstring): reset the sandbox working tree before each attempt
        so every attempt's diff is that attempt's OWN full candidate patch."""
        _git(self.sandbox_root, "reset", "--hard", "HEAD")
        _git(self.sandbox_root, "clean", "-fd")

    def test_clean_first_attempt_accepts(self):
        calls = []

        def invoke(incident, diagnosis, sandbox_root, *, attempt, prior_rejection, model):
            calls.append((attempt, prior_rejection, model))
            self._reset_sandbox()
            _write(self.sandbox_root / "tests" / "test_new_bug.py", "# bug test\n")
            _write(self.sandbox_root / "native" / "engine" / "Foo.cpp", "// v2 - fixed\n")
            return {
                "bugtest": {
                    "cmd": "ctest",
                    "args": ["-R", "test_new_bug"],
                    "expectRedWithoutFix": True,
                },
                "redExitCodes": [1, 1],
                "greenExitCodes": [0, 0],
            }

        result = run_repair(
            self.incident, self.diagnosis, self.sandbox_root,
            policy_obj=_policy_with_overrides(max_repair_attempts=3), invoke=invoke,
        )

        self.assertEqual(len(calls), 1, "invoke must be called exactly once on a clean first attempt")
        # Repair routing follows the shipped law's modelRouting.repair, re-routed
        # to glm on 2026-08-15 ("the thinking brain shifts over to you" - Opus now
        # reviews after the process, it no longer repairs inside it).
        self.assertEqual(calls[0], (1, None, "glm"))
        self.assertTrue(result["accepted"])
        self.assertEqual(result["attempts"], 1)
        self.assertEqual(result["incidentId"], "AR-test-0006")
        self.assertIn("tests/test_new_bug.py", result["classification"]["testAdds"])
        self.assertEqual(result["classification"]["forbidden"], [])

    def test_rejected_attempt_then_clean_attempt_feeds_rejection_verbatim(self):
        calls = []

        def invoke(incident, diagnosis, sandbox_root, *, attempt, prior_rejection, model):
            calls.append((attempt, prior_rejection))
            self._reset_sandbox()
            if attempt == 1:
                # No test added at all - violates ruling 5 (the bug-test door).
                _write(self.sandbox_root / "native" / "engine" / "Foo.cpp", "// v2 - fixed\n")
                return {
                    "bugtest": {
                        "cmd": "ctest",
                        "args": ["-R", "test_new_bug"],
                        "expectRedWithoutFix": True,
                    },
                    "redExitCodes": [1, 1],
                    "greenExitCodes": [0, 0],
                }
            _write(self.sandbox_root / "tests" / "test_new_bug.py", "# bug test\n")
            _write(self.sandbox_root / "native" / "engine" / "Foo.cpp", "// v2 - fixed\n")
            return {
                "bugtest": {
                    "cmd": "ctest",
                    "args": ["-R", "test_new_bug"],
                    "expectRedWithoutFix": True,
                },
                "redExitCodes": [1, 1],
                "greenExitCodes": [0, 0],
            }

        result = run_repair(
            self.incident, self.diagnosis, self.sandbox_root,
            policy_obj=_policy_with_overrides(max_repair_attempts=3), invoke=invoke,
        )

        self.assertEqual(len(calls), 2, "invoke must be retried exactly once after one rejection")
        first_attempt_num, first_prior = calls[0]
        second_attempt_num, second_prior = calls[1]
        self.assertEqual((first_attempt_num, first_prior), (1, None))
        self.assertEqual(second_attempt_num, 2)
        # VERBATIM: the second call's prior_rejection must be exactly the first
        # rejection's own message - ruling 5's own wording included, not paraphrased.
        self.assertIsNotNone(second_prior)
        self.assertIn("ruling 5", second_prior)
        self.assertIn("bug-test door", second_prior)

        self.assertTrue(result["accepted"])
        self.assertEqual(result["attempts"], 2)
        self.assertEqual(len(result["attemptLog"]), 2)
        self.assertFalse(result["attemptLog"][0]["accepted"])
        self.assertTrue(result["attemptLog"][1]["accepted"])

    def test_budget_exhaustion_after_all_attempts_reject(self):
        calls = []

        def invoke(incident, diagnosis, sandbox_root, *, attempt, prior_rejection, model):
            calls.append(attempt)
            self._reset_sandbox()
            # Every attempt forgets to add a bug test - permanently rejected.
            _write(self.sandbox_root / "native" / "engine" / "Foo.cpp", f"// attempt {attempt}\n")
            return {
                "bugtest": {
                    "cmd": "ctest",
                    "args": ["-R", "test_new_bug"],
                    "expectRedWithoutFix": True,
                },
                "redExitCodes": [1, 1],
                "greenExitCodes": [0, 0],
            }

        result = run_repair(
            self.incident, self.diagnosis, self.sandbox_root,
            policy_obj=_policy_with_overrides(max_repair_attempts=2), invoke=invoke,
        )

        self.assertEqual(calls, [1, 2], "must try exactly maxRepairAttempts times, no more")
        self.assertFalse(result["accepted"])
        self.assertEqual(result["attempts"], 2)
        self.assertIn("exhausted", result["escalateReason"])
        self.assertIn("ruling 5", result["escalateReason"])

    def test_zero_max_attempts_escalates_without_ever_calling_invoke(self):
        def invoke(*args, **kwargs):
            raise AssertionError("invoke() must never be called when maxRepairAttempts is 0")

        result = run_repair(
            self.incident, self.diagnosis, self.sandbox_root,
            policy_obj=_CannedPolicy(max_repair_attempts=0), invoke=invoke,
        )

        self.assertFalse(result["accepted"])
        self.assertEqual(result["attempts"], 0)
        self.assertEqual(result["attemptLog"], [])
        self.assertIn("0", result["escalateReason"])

    def test_default_invoke_is_deferred_and_raises_loudly(self):
        """The DEFERRED boundary must be loud, not silent: calling the real (unwired) live
        Sonnet repair invocation raises NotImplementedError rather than fabricating a
        patch or exit codes."""
        with self.assertRaises(NotImplementedError):
            contract_mod.default_invoke(
                self.incident, self.diagnosis, self.sandbox_root,
                attempt=1, prior_rejection=None, model="sonnet",
            )

    def test_run_repair_default_parameter_is_default_invoke(self):
        """run_repair() with no invoke override must reach for the deferred seam, not a
        silently-working stub - proves the wiring, not just the standalone function."""
        with self.assertRaises(NotImplementedError):
            run_repair(
                self.incident, self.diagnosis, self.sandbox_root,
                policy_obj=_CannedPolicy(max_repair_attempts=1),
            )


if __name__ == "__main__":
    print(f"[test_autorepair_contract] script under test: {CONTRACT_SCRIPT_PATH}")
    unittest.main(verbosity=2)

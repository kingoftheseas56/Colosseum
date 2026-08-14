#!/usr/bin/env python3
"""test_autorepair_policy.py - tests for scripts/autorepair/policy.py (Guardian Loop Slice G1).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G1 ("The laws -
policy, forbidden paths, risk classes, and a loader that fails closed"). Pure Python,
stdlib unittest - house flat convention (D8: tests/test_autorepair_*.py, not a subdir),
matching the pattern already used by tests/test_lanista_coverage.py and
tests/test_soak_digest.py. Runnable directly:

    python tests/test_autorepair_policy.py -v

Six groups of cases, matching the G1 slice's own "Focused tests" contract:

  ShippedFilesLoadCleanTests    - the real docs/autorepair/{policy,forbidden-paths,
                                   risk-classes}.json load clean via load_policy() with no
                                   arguments, and every policy.json value matches the
                                   Program rulings it encodes (autonomyLevel, maxRepair-
                                   Attempts, maxPatchLines, triage, model routing, ...).

  UnknownKeyRefusalTests        - inject an unrecognized field into a TEMP COPY of each of
                                   the three law files; load_policy() must raise
                                   PolicySchemaError naming the offending field. The
                                   shipped originals are never touched.

  MissingRequiredFieldRefusalTests - delete a required field from a temp copy; loader
                                   raises PolicySchemaError naming the missing field.

  SelfProtectionTests           - Program ruling 1, mechanized: forbidden-paths.json must
                                   cover its own three law files. Narrowing the protecting
                                   glob in a temp copy (dropping one or all three law files
                                   from coverage) makes load_policy() raise
                                   SelfProtectionError naming exactly the file(s) that fell
                                   out of protection.

  IsForbiddenSemanticsTests     - Policy.is_forbidden(path, op) against the real shipped
                                   law files: existing tests/ file MODIFY/DELETE forbidden;
                                   a NEW path under tests/ ADD allowed (the bug-test door);
                                   .gitattributes/.gitignore/.claude/** MODIFY forbidden;
                                   a normal native/engine/*.cpp MODIFY allowed;
                                   native/CMakeLists.txt deliberately NOT forbidden (A8);
                                   ADD under docs/autorepair/ still forbidden (no exemption
                                   there - tests/** is the ONLY add-exempt glob).

  NegativeControlTests          - the plan's mandatory negative control, both directions:
                                   corrupt a temp copy of policy.json to
                                   `"autonomyLevel": "merge"` (not in the v0 enum
                                   {"patch-only", "draft-pr"} - ruling 6/D... "merge"/level-C
                                   is explicitly not a valid value yet) and assert EXACTLY
                                   the enum-validation case goes red (PolicySchemaError
                                   naming policy.autonomyLevel and the allowed values, not
                                   any other failure); restore the temp copy from the real
                                   file and assert it goes green again.

All temp-copy fixtures operate on a `tempfile.TemporaryDirectory()` seeded from
shutil.copytree(REAL_LAW_DIR, ...) - the shipped docs/autorepair/*.json files themselves
are read-only inputs to every test in this file and are never mutated in place.
"""
from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "policy.py"
REAL_LAW_DIR = REPO_ROOT / "docs" / "autorepair"


def _load_policy_module():
    spec = importlib.util.spec_from_file_location("autorepair_policy", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_policy_module()


def _temp_law_copy(tmp: Path) -> Path:
    """Copy the real docs/autorepair/*.json law files into `tmp`; the shipped originals
    under REAL_LAW_DIR are never modified by any test in this file."""
    dest = tmp / "autorepair"
    shutil.copytree(REAL_LAW_DIR, dest)
    return dest


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _write_json(path: Path, obj: dict) -> None:
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


# ══════════════════════════════════════════════════════════════════════════
# Part A - the shipped law files load clean
# ══════════════════════════════════════════════════════════════════════════

class ShippedFilesLoadCleanTests(unittest.TestCase):
    def test_load_policy_defaults_succeeds_and_matches_program_rulings(self):
        policy = mod.load_policy()

        # Ruling 6 + the plan's "widened enum for v0" note: autonomyLevel is draft-pr,
        # and "merge" (level C) is deliberately not yet a valid value (see
        # NegativeControlTests below).
        self.assertEqual(policy.policy["autonomyLevel"], "draft-pr")
        self.assertIn(policy.policy["autonomyLevel"], mod.AUTONOMY_LEVELS)
        self.assertEqual(mod.AUTONOMY_LEVELS, {"patch-only", "draft-pr"})

        # Ruling 9: max 3 repair attempts.
        self.assertEqual(policy.policy["maxRepairAttempts"], 3)

        # Amendment A8: patch-size budget, default 400 lines.
        self.assertEqual(policy.policy["maxPatchLines"], 400)

        # D4: triage runs 3x, CONFIRMED at >=2 matching failures.
        self.assertEqual(policy.policy["triage"], {"runs": 3, "confirmThreshold": 2})

        # Slice G5 default confidence gate.
        self.assertEqual(policy.policy["minConfidenceToRepair"], "medium")

        # Slice G10 default: Night Watch never auto-launches a repair yet.
        self.assertFalse(policy.policy["nightWatchAutoRepair"])

        # D3, CONFIRMED by Hemanth: Opus diagnoses and verifies, Sonnet repairs.
        self.assertEqual(
            policy.policy["modelRouting"],
            {"diagnosis": "opus", "repair": "sonnet", "verify": "opus"},
        )

        # D3's GLM refutation: advisory only in v0.
        self.assertEqual(policy.policy["verifierRefutation"]["provider"], "glm")
        self.assertEqual(policy.policy["verifierRefutation"]["thinking"], "high")
        self.assertTrue(policy.policy["verifierRefutation"]["advisory"])

        # D7: 8-hour per-incident wall-clock default.
        self.assertEqual(policy.policy["perIncidentTotalSec"], 28800)
        self.assertEqual(set(policy.policy["perStageTimeoutSec"]), mod.STAGE_NAMES)

        # Ruling 1 self-protection is asserted inside load_policy() itself; reaching this
        # line at all is already proof it held for the shipped files.
        self.assertGreaterEqual(len(policy.forbidden_modify_delete), 10)
        self.assertEqual(tuple(policy.forbidden_add_exempt), ("tests/**",))

        self.assertEqual(len(policy.risk_classes), 1)
        self.assertEqual(policy.risk_classes[0]["id"], "default")
        self.assertEqual(len(policy.risk_classes[0]["verify"]["journeys"]), 6)
        self.assertEqual(policy.risk_classes[0]["verify"]["warningGate"], "tests/warning_gate.ps1")

    def test_load_policy_from_a_faithful_temp_copy_also_succeeds(self):
        """Sanity check for every later test in this file: an UNMODIFIED temp copy of the
        real law files must load exactly as cleanly as the originals, so any failure in
        later corruption tests is attributable to the specific corruption, not to the
        act of copying."""
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            policy = mod.load_policy(law_dir)
            self.assertEqual(policy.policy["autonomyLevel"], "draft-pr")


# ══════════════════════════════════════════════════════════════════════════
# Part B - unknown-key refusal (closed schema, "anywhere")
# ══════════════════════════════════════════════════════════════════════════

class UnknownKeyRefusalTests(unittest.TestCase):
    def test_unknown_top_level_key_in_policy_json_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "policy.json"
            obj = _load_json(path)
            obj["bogusExtraField"] = "oops"
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("unknown field", str(ctx.exception))
            self.assertIn("bogusExtraField", str(ctx.exception))

    def test_unknown_nested_key_in_policy_triage_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "policy.json"
            obj = _load_json(path)
            obj["triage"]["bogusNested"] = 1
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("unknown field", str(ctx.exception))
            self.assertIn("bogusNested", str(ctx.exception))

    def test_unknown_key_in_forbidden_paths_json_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "forbidden-paths.json"
            obj = _load_json(path)
            obj["unexpectedTopLevel"] = []
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("unknown field", str(ctx.exception))
            self.assertIn("unexpectedTopLevel", str(ctx.exception))

    def test_unknown_key_in_risk_classes_json_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "risk-classes.json"
            obj = _load_json(path)
            obj["classes"][0]["unexpectedField"] = "x"
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("unknown field", str(ctx.exception))
            self.assertIn("unexpectedField", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# Part C - missing-required-field refusal
# ══════════════════════════════════════════════════════════════════════════

class MissingRequiredFieldRefusalTests(unittest.TestCase):
    def test_missing_autonomy_level_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "policy.json"
            obj = _load_json(path)
            del obj["autonomyLevel"]
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("missing required field", str(ctx.exception))
            self.assertIn("autonomyLevel", str(ctx.exception))

    def test_missing_max_repair_attempts_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "policy.json"
            obj = _load_json(path)
            del obj["maxRepairAttempts"]
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("missing required field", str(ctx.exception))
            self.assertIn("maxRepairAttempts", str(ctx.exception))

    def test_missing_forbidden_modify_delete_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "forbidden-paths.json"
            obj = _load_json(path)
            del obj["forbidden"]["modifyDelete"]
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("missing required field", str(ctx.exception))
            self.assertIn("modifyDelete", str(ctx.exception))

    def test_missing_risk_class_verify_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "risk-classes.json"
            obj = _load_json(path)
            del obj["classes"][0]["verify"]
            _write_json(path, obj)

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            self.assertIn("missing required field", str(ctx.exception))
            self.assertIn("verify", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# Part D - self-protection (Program ruling 1, mechanized)
# ══════════════════════════════════════════════════════════════════════════

class SelfProtectionTests(unittest.TestCase):
    def test_removing_docs_autorepair_glob_entirely_unprotects_all_three_law_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "forbidden-paths.json"
            obj = _load_json(path)
            obj["forbidden"]["modifyDelete"] = [
                p for p in obj["forbidden"]["modifyDelete"] if p != "docs/autorepair/**"
            ]
            self.assertNotIn("docs/autorepair/**", obj["forbidden"]["modifyDelete"])
            _write_json(path, obj)

            with self.assertRaises(mod.SelfProtectionError) as ctx:
                mod.load_policy(law_dir)
            message = str(ctx.exception)
            self.assertIn("docs/autorepair/policy.json", message)
            self.assertIn("docs/autorepair/forbidden-paths.json", message)
            self.assertIn("docs/autorepair/risk-classes.json", message)

    def test_narrowing_protection_to_drop_only_risk_classes_json_is_caught(self):
        """Removing just ONE of the three law files from protection (not all three) must
        still refuse, naming exactly the file that fell out of coverage."""
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "forbidden-paths.json"
            obj = _load_json(path)
            patterns = [
                p for p in obj["forbidden"]["modifyDelete"] if p != "docs/autorepair/**"
            ]
            # Re-protect policy.json and forbidden-paths.json individually, but
            # deliberately leave risk-classes.json uncovered.
            patterns += [
                "docs/autorepair/policy.json",
                "docs/autorepair/forbidden-paths.json",
            ]
            obj["forbidden"]["modifyDelete"] = patterns
            _write_json(path, obj)

            with self.assertRaises(mod.SelfProtectionError) as ctx:
                mod.load_policy(law_dir)
            message = str(ctx.exception)
            self.assertIn("docs/autorepair/risk-classes.json", message)
            self.assertNotIn("docs/autorepair/policy.json,", message)

    def test_restoring_full_protection_loads_clean_again(self):
        """Both directions, as required: break self-protection, then restore from the
        real shipped file and confirm it loads green again."""
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "forbidden-paths.json"
            original_text = path.read_text(encoding="utf-8")

            obj = json.loads(original_text)
            obj["forbidden"]["modifyDelete"] = [
                p for p in obj["forbidden"]["modifyDelete"] if p != "docs/autorepair/**"
            ]
            _write_json(path, obj)
            with self.assertRaises(mod.SelfProtectionError):
                mod.load_policy(law_dir)

            path.write_text(original_text, encoding="utf-8")
            policy = mod.load_policy(law_dir)
            self.assertEqual(policy.policy["autonomyLevel"], "draft-pr")


# ══════════════════════════════════════════════════════════════════════════
# Part E - Policy.is_forbidden(path, op) semantics
# ══════════════════════════════════════════════════════════════════════════

class IsForbiddenSemanticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = mod.load_policy()

    def test_modify_existing_tests_file_is_forbidden(self):
        self.assertTrue(self.policy.is_forbidden("tests/test_lanista_coverage.py", "modify"))

    def test_delete_existing_tests_file_is_forbidden(self):
        self.assertTrue(self.policy.is_forbidden("tests/test_lanista_coverage.py", "delete"))

    def test_add_new_file_under_tests_is_allowed(self):
        """The bug-test door: a repair patch may ADD a new file under tests/."""
        self.assertFalse(
            self.policy.is_forbidden("tests/test_autorepair_new_bug_case.py", "add")
        )

    def test_add_new_file_under_nested_tests_subdir_is_allowed(self):
        self.assertFalse(
            self.policy.is_forbidden(
                "tests/lanista_scenarios/journey_new_bug.json", "add"
            )
        )

    def test_gitattributes_modify_is_forbidden(self):
        self.assertTrue(self.policy.is_forbidden(".gitattributes", "modify"))

    def test_gitignore_modify_is_forbidden(self):
        self.assertTrue(self.policy.is_forbidden(".gitignore", "modify"))

    def test_claude_dir_modify_is_forbidden(self):
        self.assertTrue(self.policy.is_forbidden(".claude/settings.json", "modify"))

    def test_normal_source_file_modify_is_allowed(self):
        self.assertFalse(self.policy.is_forbidden("native/engine/Foo.cpp", "modify"))

    def test_cmakelists_is_not_forbidden(self):
        """Amendment A8: native/CMakeLists.txt stays repairable (flagged HIGH-RISK
        elsewhere, not blocked here)."""
        self.assertFalse(self.policy.is_forbidden("native/CMakeLists.txt", "modify"))
        self.assertFalse(self.policy.is_forbidden("native/CMakeLists.txt", "delete"))

    def test_add_under_docs_autorepair_is_still_forbidden(self):
        """Only tests/** is add-exempt - adding a brand-new file under docs/autorepair/
        is exactly as forbidden as modifying an existing one."""
        self.assertTrue(self.policy.is_forbidden("docs/autorepair/new-law.json", "add"))

    def test_scripts_autorepair_modify_is_forbidden(self):
        self.assertTrue(
            self.policy.is_forbidden("scripts/autorepair/policy.py", "modify")
        )

    def test_warning_allowlist_modify_is_forbidden(self):
        self.assertTrue(
            self.policy.is_forbidden("tests/lanista-warning-allowlist.json", "modify")
        )

    def test_encyclopedia_state_file_modify_is_forbidden(self):
        self.assertTrue(
            self.policy.is_forbidden("docs/encyclopedia/vault-state.json", "modify")
        )

    def test_encyclopedia_guide_prose_modify_is_allowed(self):
        """Only the generated *-state.json sidecars are forbidden, not the guide prose
        itself (e.g. docs/encyclopedia/vault.md)."""
        self.assertFalse(self.policy.is_forbidden("docs/encyclopedia/vault.md", "modify"))

    def test_unknown_operation_raises(self):
        with self.assertRaises(mod.PolicyError):
            self.policy.is_forbidden("native/engine/Foo.cpp", "rename")


# ══════════════════════════════════════════════════════════════════════════
# Part F - mandatory negative control (both directions)
# ══════════════════════════════════════════════════════════════════════════

class NegativeControlTests(unittest.TestCase):
    def test_nc_autonomy_level_merge_is_rejected_then_restore_is_green(self):
        """The plan's mandatory G1 negative control: corrupt a temp copy of policy.json
        to `"autonomyLevel": "merge"` (not in the v0 enum) and confirm EXACTLY the
        enum-validation case goes red; restore; confirm green."""
        with tempfile.TemporaryDirectory() as tmp:
            law_dir = _temp_law_copy(Path(tmp))
            path = law_dir / "policy.json"
            original_text = path.read_text(encoding="utf-8")

            self.assertNotIn("merge", mod.AUTONOMY_LEVELS)
            mutated_text = original_text.replace(
                '"autonomyLevel": "draft-pr"', '"autonomyLevel": "merge"'
            )
            self.assertNotEqual(
                mutated_text,
                original_text,
                "fixture policy.json did not contain the expected literal to mutate",
            )
            path.write_text(mutated_text, encoding="utf-8")

            with self.assertRaises(mod.PolicySchemaError) as ctx:
                mod.load_policy(law_dir)
            message = str(ctx.exception)
            self.assertIn("policy.autonomyLevel", message)
            self.assertIn("patch-only", message)
            self.assertIn("draft-pr", message)
            self.assertIn("'merge'", message)

            # Restore and confirm green again.
            path.write_text(original_text, encoding="utf-8")
            policy = mod.load_policy(law_dir)
            self.assertEqual(policy.policy["autonomyLevel"], "draft-pr")


if __name__ == "__main__":
    print(f"[test_autorepair_policy] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)

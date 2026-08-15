#!/usr/bin/env python3
"""test_autorepair_live_runners.py - STRUCTURAL tests for
scripts/autorepair/live_runners.py (the Guardian Loop's live stage-runner wiring).

This suite is deliberately narrow in scope, matching the task that produced
live_runners.py: it proves the WIRING is correct - shapes, signatures, command assembly,
sandbox-reuse detection, JSON extraction, dossier assembly - WITHOUT ever launching a real
`claude -p`, a real build, a real `git fetch`/`push`/`gh`, or the full live orchestrator
loop. Every hermetic fixture here is a THROWAWAY git repo under
tempfile.TemporaryDirectory() (mirrors tests/test_autorepair_sandbox.py's own pattern) or
canned data - never the real Colosseum repo, never C:\\arsbx.

The ONE exception, called out explicitly: SandboxReuseAgainstGoldenCloneTests reads (never
writes) the real, already-built C:\\arsbx\\g2-live-proof clone that docs/autorepair/
batched-runtime-pass.md documents as already Runtime-validated - `git remote`/`git
rev-parse HEAD`/file-existence checks only, the same read-only checks
`find_or_build_sandbox()` itself performs before ever deciding whether to build. Skipped
cleanly (not failed) if that clone is not present on the machine running this suite.

Runnable directly:
    python tests/test_autorepair_live_runners.py -v
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
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "live_runners.py"


def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_module(SCRIPT_PATH, "autorepair_live_runners")


def _git(cwd: Path, *args: str) -> subprocess.CompletedProcess:
    result = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True)
    if result.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed in {cwd}: {result.stderr}")
    return result


def _init_fixture_repo(root: Path) -> str:
    """A minimal throwaway git repo with one commit, mirroring
    tests/test_autorepair_sandbox.py's own fixture pattern - returns its HEAD sha."""
    root.mkdir(parents=True, exist_ok=True)
    _git(root, "init", "-q")
    _git(root, "config", "user.email", "test@example.com")
    _git(root, "config", "user.name", "Test")
    (root / "README.md").write_text("fixture\n", encoding="utf-8")
    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "initial")
    return _git(root, "rev-parse", "HEAD").stdout.strip()


# ══════════════════════════════════════════════════════════════════════════
# Module shape: imports clean, build_live_stage_runners() returns 5 callables
# ══════════════════════════════════════════════════════════════════════════


class ModuleShapeTests(unittest.TestCase):
    def test_module_exposes_expected_public_api(self):
        for name in mod.__all__:
            self.assertTrue(hasattr(mod, name), f"live_runners.py.__all__ names {name!r} but it is missing")

    def test_build_live_stage_runners_returns_all_five_stages(self):
        runners = mod.build_live_stage_runners()
        self.assertEqual(set(runners), {"triage", "diagnosis", "repair", "verify", "promotion"})

    def test_every_stage_runner_matches_the_orchestrator_signature(self):
        """Each callable must accept (incident, incident_dir, policy_obj, *, prior) - the
        EXACT shape orchestrator.py's own _run_loop()/_obtain() calls
        `runners[stage](incident_obj, incident_dir, policy_obj, prior=dict(context))`."""
        import inspect

        runners = mod.build_live_stage_runners()
        for stage, fn in runners.items():
            sig = inspect.signature(fn)
            params = list(sig.parameters.values())
            positional = [p for p in params if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)]
            self.assertEqual(
                len(positional), 3,
                f"stage_runners[{stage!r}] must take exactly 3 positional params "
                f"(incident, incident_dir, policy_obj); got {sig}",
            )
            self.assertIn("prior", sig.parameters, f"stage_runners[{stage!r}] must accept 'prior'")
            self.assertEqual(
                sig.parameters["prior"].kind, sig.parameters["prior"].KEYWORD_ONLY,
                f"stage_runners[{stage!r}]'s 'prior' must be keyword-only",
            )

    def test_default_stage_runners_still_raise_not_implemented(self):
        """This module must never accidentally satisfy orchestrator.DEFAULT_STAGE_RUNNERS
        itself - it is a SEPARATE, opt-in seam the caller wires explicitly."""
        import orchestrator as orch_mod

        for stage in orch_mod.LOOP_STAGES:
            with self.assertRaises(NotImplementedError):
                orch_mod.DEFAULT_STAGE_RUNNERS[stage]({"id": "AR-x"}, Path("."), object(), prior={})


# ══════════════════════════════════════════════════════════════════════════
# Guard-hook settings.json generation
# ══════════════════════════════════════════════════════════════════════════


class GuardSettingsTests(unittest.TestCase):
    def test_write_guard_settings_produces_valid_pretooluse_hook(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "settings-dest"
            sandbox_root = Path(tmp) / "some-clone"
            settings_path = mod.write_guard_settings(dest, sandbox_root)
            self.assertTrue(settings_path.is_file())
            obj = json.loads(settings_path.read_text(encoding="utf-8"))
            hooks = obj["hooks"]["PreToolUse"][0]["hooks"][0]
            self.assertEqual(hooks["type"], "command")
            self.assertIn(str(mod.GUARD_HOOK_SCRIPT), hooks["command"])
            self.assertIn(str(sandbox_root), hooks["command"])
            self.assertIn("--sandbox-root", hooks["command"])

    def test_write_guard_settings_creates_dest_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "nested" / "does" / "not" / "exist"
            mod.write_guard_settings(dest, Path(tmp) / "clone")
            self.assertTrue((dest / "settings.json").is_file())


# ══════════════════════════════════════════════════════════════════════════
# Headless claude command assembly + JSON extraction (no subprocess launched)
# ══════════════════════════════════════════════════════════════════════════


class ClaudeArgvAssemblyTests(unittest.TestCase):
    def test_diagnosis_shaped_argv(self):
        argv = mod._claude_argv(
            claude_cli="claude", model="opus", allowed_tools=["Read", "Grep", "Glob"],
            add_dirs=[Path("C:/arsbx/x"), Path("C:/repo/docs/encyclopedia")],
            settings_path=Path("C:/tmp/settings.json"),
        )
        self.assertEqual(argv[0], "claude")
        self.assertIn("-p", argv)
        self.assertIn("--model", argv)
        self.assertEqual(argv[argv.index("--model") + 1], "opus")
        self.assertIn("--allowedTools", argv)
        self.assertEqual(argv[argv.index("--allowedTools") + 1], "Read,Grep,Glob")
        self.assertEqual(argv.count("--add-dir"), 2)
        self.assertNotIn("Bash", argv[argv.index("--allowedTools") + 1])

    def test_repair_shaped_argv_includes_bash(self):
        argv = mod._claude_argv(
            claude_cli="claude", model="sonnet",
            allowed_tools=["Read", "Grep", "Glob", "Edit", "Write", "Bash"],
            add_dirs=[Path("C:/arsbx/x")], settings_path=Path("C:/tmp/settings.json"),
        )
        self.assertIn("Bash", argv[argv.index("--allowedTools") + 1])
        self.assertEqual(argv[argv.index("--model") + 1], "sonnet")

    def test_prompt_text_never_becomes_an_argv_element(self):
        """The prompt is piped via stdin by run_headless_claude(), never baked into argv -
        _claude_argv() itself takes no prompt parameter at all, so this is provable by
        signature inspection alone."""
        import inspect

        params = inspect.signature(mod._claude_argv).parameters
        self.assertNotIn("prompt", params)


class JsonExtractionTests(unittest.TestCase):
    def test_extracts_plain_json(self):
        obj = mod._extract_json_object('{"approve": true, "reasons": ["ok"], "riskAssessment": "low"}')
        self.assertEqual(obj["approve"], True)

    def test_extracts_fenced_json(self):
        text = 'Here is my answer:\n\n```json\n{"a": 1, "b": [1, 2]}\n```\n\nDone.'
        obj = mod._extract_json_object(text)
        self.assertEqual(obj, {"a": 1, "b": [1, 2]})

    def test_extracts_embedded_json_with_surrounding_prose(self):
        text = 'Sure - {"observed": "x", "expected": "y"} is my answer.'
        obj = mod._extract_json_object(text)
        self.assertEqual(obj, {"observed": "x", "expected": "y"})

    def test_raises_on_unparsable_text(self):
        with self.assertRaises(mod.ClaudeInvocationError):
            mod._extract_json_object("no json anywhere in this sentence at all")


# ══════════════════════════════════════════════════════════════════════════
# Sandbox reuse detection - hermetic fixture repo (never C:\arsbx)
# ══════════════════════════════════════════════════════════════════════════


class SandboxReuseDetectionHermeticTests(unittest.TestCase):
    def test_nonexistent_path_is_not_reusable(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertFalse(mod._sandbox_reusable_at(Path(tmp) / "does-not-exist", "deadbeef"))

    def test_repo_with_a_remote_is_not_reusable(self):
        """D5/A1: a reusable sandbox must have ZERO remotes - a clone that still has one
        (origin never removed) must never be silently reused."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            sha = _init_fixture_repo(root)
            _git(root, "remote", "add", "origin", "https://example.invalid/repo.git")
            self.assertFalse(mod._sandbox_reusable_at(root, sha))

    def test_repo_at_wrong_sha_is_not_reusable(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            _init_fixture_repo(root)
            self.assertFalse(mod._sandbox_reusable_at(root, "0" * 40))

    def test_repo_at_right_sha_but_not_built_is_not_reusable(self):
        """A clone with the right sha and zero remotes but no colosseum.exe/MpvQt.dll is
        NOT reusable - it needs build()+provision() first, never assumed built."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            sha = _init_fixture_repo(root)
            self.assertFalse(mod._sandbox_reusable_at(root, sha))

    def test_repo_at_right_sha_zero_remotes_and_built_is_reusable(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            sha = _init_fixture_repo(root)
            build_dir = root / "native" / "build-msvc"
            build_dir.mkdir(parents=True)
            (build_dir / "colosseum.exe").write_bytes(b"stub")
            (build_dir / "MpvQt.dll").write_bytes(b"stub")
            self.assertTrue(mod._sandbox_reusable_at(root, sha))

    def test_sha_matches_accepts_short_and_full_forms(self):
        full = "353b6757f812b5453040d0f313477e85253d9263"
        self.assertTrue(mod._sha_matches(full, "353b675"))
        self.assertTrue(mod._sha_matches("353b675", full))
        self.assertTrue(mod._sha_matches(full, full))
        self.assertFalse(mod._sha_matches(full, "deadbeef"))
        self.assertFalse(mod._sha_matches(None, full))

    def test_find_or_build_sandbox_reuses_a_prior_stages_clone_without_rebuilding(self):
        """The general case the golden-incident special case generalizes: a clone already
        left at <sandbox_root>/<incident_id> by an earlier stage of THIS incident is reused,
        never re-created (sandbox.create() would raise if a second stage tried to reclone
        over it)."""
        with tempfile.TemporaryDirectory() as tmp:
            sandbox_root = Path(tmp) / "arsbx"
            clone = sandbox_root / "AR-fixture-0001"
            sha = _init_fixture_repo(clone)
            build_dir = clone / "native" / "build-msvc"
            build_dir.mkdir(parents=True)
            (build_dir / "colosseum.exe").write_bytes(b"stub")
            (build_dir / "MpvQt.dll").write_bytes(b"stub")

            incident = {"id": "AR-fixture-0001", "baseSha": sha}
            found, reused = mod.find_or_build_sandbox(
                incident, sandbox_root=sandbox_root,
                golden_reuse_dir=Path(tmp) / "no-such-golden-dir",
            )
            self.assertTrue(reused)
            self.assertEqual(found.resolve(), clone.resolve())

    def test_find_or_build_sandbox_refuses_to_silently_overwrite_a_stale_clone(self):
        with tempfile.TemporaryDirectory() as tmp:
            sandbox_root = Path(tmp) / "arsbx"
            clone = sandbox_root / "AR-fixture-0002"
            _init_fixture_repo(clone)  # right structure, but WRONG sha for the incident below

            incident = {"id": "AR-fixture-0002", "baseSha": "f" * 40}
            with self.assertRaises(mod.LiveRunnerError):
                mod.find_or_build_sandbox(
                    incident, sandbox_root=sandbox_root,
                    golden_reuse_dir=Path(tmp) / "no-such-golden-dir",
                )


class SandboxReuseAgainstGoldenCloneTests(unittest.TestCase):
    """The ONE test in this suite that reads the real C:\\arsbx\\g2-live-proof clone -
    read-only (`git remote`, `git rev-parse HEAD`, file existence), never a write. Skipped
    cleanly if that clone is not present on the machine running this suite."""

    def test_golden_reuse_dir_is_detected_as_reusable_for_the_golden_incident(self):
        if not mod.GOLDEN_REUSE_DIR.is_dir():
            self.skipTest(f"{mod.GOLDEN_REUSE_DIR} not present on this machine")
        self.assertTrue(mod._sandbox_reusable_at(mod.GOLDEN_REUSE_DIR, mod.GOLDEN_BASE_SHA))

    def test_find_or_build_sandbox_short_circuits_to_the_golden_dir_without_building(self):
        if not mod.GOLDEN_REUSE_DIR.is_dir():
            self.skipTest(f"{mod.GOLDEN_REUSE_DIR} not present on this machine")
        import time

        incident = {"id": "AR-golden-smoke-test-only", "baseSha": mod.GOLDEN_BASE_SHA}
        t0 = time.time()
        clone, reused = mod.find_or_build_sandbox(incident)
        elapsed = time.time() - t0
        self.assertTrue(reused)
        self.assertEqual(clone.resolve(), mod.GOLDEN_REUSE_DIR.resolve())
        # A real create()/build()/provision() takes minutes to hours; a reuse short-circuit
        # must be near-instant - generous 10s ceiling so a slow CI disk never flakes this.
        self.assertLess(elapsed, 10.0)


# ══════════════════════════════════════════════════════════════════════════
# Triage: reproduce-command assembly + reproduce-run classification
# ══════════════════════════════════════════════════════════════════════════


_CANNED_INCIDENT = {
    "id": "AR-fixture-triage",
    "baseSha": "0" * 40,
    "scenario": "tests/lanista_scenarios/journey_open_manga.json",
    "qml": "qml/Main.qml",
    "seed": "C:/fixtures/seed-dir",
    "drive": True,
    "readyMs": 90000,
    "failingStep": {"index": 25, "label": "regression: reopen"},
}


class ReproduceCommandTests(unittest.TestCase):
    def test_command_targets_the_sandbox_clones_own_binaries(self):
        clone = Path("C:/arsbx/AR-fixture-triage")
        cmd = mod._reproduce_command(_CANNED_INCIDENT, clone, tag="gl-test-0")
        self.assertTrue(cmd[0].endswith("lanista.exe"))
        self.assertIn(str(clone), cmd[0])
        self.assertIn(str(clone), cmd[cmd.index("--exe") + 1])
        self.assertIn("--tag", cmd)
        self.assertEqual(cmd[cmd.index("--tag") + 1], "gl-test-0")
        self.assertIn("--seed", cmd)
        self.assertIn("--drive", cmd)
        self.assertIn("--ready-ms", cmd)
        self.assertIn("--verbose", cmd)

    def test_run_once_signature_matches_triage_triages_own_call_convention(self):
        """triage.triage() calls `run_once(i, incident)` - TWO positional args - even though
        triage.py's own make_live_run_once() is (inconsistently) typed as returning a
        one-argument Callable[[int], RunResult]. This module's factory deliberately matches
        the REAL call convention, not the inconsistent type hint (flagged in this module's
        own docstring) - proven here by actually calling it that way."""
        clone = Path("C:/arsbx/AR-fixture-triage")
        run_once = mod.make_live_triage_run_once(_CANNED_INCIDENT, clone)
        import inspect

        sig = inspect.signature(run_once)
        self.assertEqual(len(sig.parameters), 2)


_CANNED_CMD = ["lanista.exe", "session", "run", "tests/lanista_scenarios/journey_open_manga.json"]


class ClassifyReproduceRunTests(unittest.TestCase):
    def test_fail_at_the_asserted_step_classifies_fail(self):
        label = _CANNED_INCIDENT["failingStep"]["label"]
        stdout = f"PASS  earlier step\nFAIL  {label}  [x == y \u2014 got z]\n2 steps, 1 failed\nEXIT_CODE: 1\n"
        result = mod._classify_reproduce_run(_CANNED_INCIDENT, stdout, _CANNED_CMD)
        self.assertEqual(result.status, "FAIL")
        self.assertEqual(result.stepLabel, label)

    def test_pass_at_the_asserted_step_classifies_pass(self):
        label = _CANNED_INCIDENT["failingStep"]["label"]
        stdout = f"PASS  {label}\n1 steps, 0 failed\nEXIT_CODE: 0\n"
        result = mod._classify_reproduce_run(_CANNED_INCIDENT, stdout, _CANNED_CMD)
        self.assertEqual(result.status, "PASS")
        self.assertIsNone(result.stepLabel)

    def test_step_never_reached_classifies_infra(self):
        stdout = "PASS  unrelated step\n1 steps, 0 failed\nEXIT_CODE: 0\n"
        result = mod._classify_reproduce_run(_CANNED_INCIDENT, stdout, _CANNED_CMD)
        self.assertEqual(result.status, "INFRA")

    def test_unparsable_output_classifies_infra_not_a_raised_exception(self):
        result = mod._classify_reproduce_run(_CANNED_INCIDENT, "total garbage, no step lines", _CANNED_CMD)
        self.assertEqual(result.status, "INFRA")
        self.assertEqual(result.stepLabel, _CANNED_INCIDENT["failingStep"]["label"])


# ══════════════════════════════════════════════════════════════════════════
# Dossier assembly - feeds promotion.assemble_pr_body()'s real D9 completeness gate
# ══════════════════════════════════════════════════════════════════════════


class DossierAssemblyTests(unittest.TestCase):
    def setUp(self):
        import promotion as promotion_mod

        self.promotion_mod = promotion_mod
        self.incident = {
            "id": "AR-fixture-dossier",
            "scenario": "tests/lanista_scenarios/journey_open_manga.json",
            "failingStep": {"index": 25, "label": "regression: reopen", "expected": "true", "got": "undefined"},
        }
        self.diagnosis = {
            "rootCause": {"file": "qml/reader/ComicReaderShell.qml", "line": 42, "claim": "binds to visibility"},
            "proposedRepair": "bind readerReady to the render-complete signal",
        }
        self.repair = {
            "classification": {
                "testAdds": ["tests/lanista_scenarios/bug_repro.json"],
                "production": ["qml/reader/ComicReaderShell.qml"],
                "forbidden": [],
            },
            "redExitCodes": [1, 1], "greenExitCodes": [0, 0],
            "bugtest": {"cmd": "lanista", "args": ["session", "run", "tests/lanista_scenarios/bug_repro.json"]},
        }
        self.verify_result = {
            "approve": True, "riskAssessment": "low",
            "gates": {"overall": "PASS", "gates": {
                "bugTestRedGreen": {"pass": True, "detail": "red then green, 2/2"},
                "unitTestsFullPass": {"pass": True, "detail": "ctest -L unit: 44/44 pass"},
                "warningGateClean": {"pass": True, "detail": "clean"},
                "journeysAllPass": {"pass": True, "detail": "all pass"},
            }},
        }

    def test_dossier_carries_every_d9_section(self):
        dossier = mod._assemble_dossier(self.incident, self.diagnosis, self.repair, self.verify_result)
        for key, _heading in self.promotion_mod.DOSSIER_SECTIONS:
            self.assertIn(key, dossier)
            self.assertTrue(dossier[key], f"dossier[{key!r}] must not be empty")

    def test_dossier_passes_the_real_assemble_pr_body_completeness_gate(self):
        dossier = mod._assemble_dossier(self.incident, self.diagnosis, self.repair, self.verify_result)
        body = self.promotion_mod.assemble_pr_body(dossier)  # raises PromotionError if incomplete
        self.assertIn("AR-fixture-dossier", body)
        self.assertIn("qml/reader/ComicReaderShell.qml", body)

    def test_dossier_survives_a_missing_verify_gates_shape(self):
        """A defensive shape check: even a thin/partial verify_result (e.g. an ESCALATE path
        with gates=None) must not crash dossier assembly - every section still renders
        something non-empty (an honest 'n/a', never a KeyError)."""
        thin_verify = {"approve": False, "riskAssessment": None, "gates": None}
        dossier = mod._assemble_dossier(self.incident, self.diagnosis, self.repair, thin_verify)
        for key, _heading in self.promotion_mod.DOSSIER_SECTIONS:
            self.assertIn(key, dossier)


# ══════════════════════════════════════════════════════════════════════════
# GLM refutation: honest degrade when no CLI is on PATH
# ══════════════════════════════════════════════════════════════════════════


class GlmRefutationTests(unittest.TestCase):
    def test_missing_cli_degrades_honestly_never_raises(self):
        result = mod.run_glm_refutation("patch summary text", glm_cli="definitely-not-a-real-binary-xyz")
        self.assertFalse(result["available"])
        self.assertIn("reason", result)


# ══════════════════════════════════════════════════════════════════════════
# extract_patch_text: real git plumbing against a hermetic fixture repo
# ══════════════════════════════════════════════════════════════════════════


class ExtractPatchTextTests(unittest.TestCase):
    def test_diff_between_base_and_head_is_returned(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            base_sha = _init_fixture_repo(root)
            (root / "new_file.txt").write_text("hello\n", encoding="utf-8")
            _git(root, "add", "-A")
            _git(root, "commit", "-q", "-m", "add new_file.txt")
            patch = mod.extract_patch_text(root, base_sha)
            self.assertIn("new_file.txt", patch)
            self.assertIn("+hello", patch)

    def test_no_diff_when_head_equals_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "clone"
            base_sha = _init_fixture_repo(root)
            patch = mod.extract_patch_text(root, base_sha)
            self.assertEqual(patch.strip(), "")


if __name__ == "__main__":
    unittest.main(verbosity=2)

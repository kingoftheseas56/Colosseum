#!/usr/bin/env python3
"""test_autorepair_sandbox.py - tests for scripts/autorepair/sandbox.py (Guardian Loop
Slice G2: "The laboratory - sandbox create/build/diff/destroy + main-repo drift
tripwire").

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G2. Pure
Python, stdlib unittest, house flat convention (D8: tests/test_autorepair_*.py) -
runnable directly:

    python tests/test_autorepair_sandbox.py -v

HERMETIC: every test builds a THROWAWAY git repository under tempfile.TemporaryDirectory()
- never the real Colosseum repo, never the real MAIN native/build-msvc, never a real
build. build()/provision() need the real MSVC/Qt toolchain and are proven separately by
the slice's LIVE end-to-end proof (a real sandbox built from the real repo); they are
intentionally NOT covered by this hermetic suite. This file mirrors the pattern already
used by tests/test_precommit_coverage_dispatch.py (real git plumbing against fixture
repos) and tests/test_autorepair_policy.py (loading a sibling script module via
importlib.util.spec_from_file_location).

Test groups:
  SandboxCreateTests              - create() clones a real fixture repo, removes the
                                     origin remote, checks out the EXACT requested sha
                                     (not just "whatever HEAD is"), and refuses to reuse
                                     an existing clone path.
  OriginRemovalNegativeControlTests - the plan's mandatory negative control, both
                                     directions: a clone where origin removal was
                                     skipped -> sandbox._assert_no_remotes() (the exact
                                     guard create() calls) refuses; remove the remote ->
                                     the same guard passes clean.
  PatchClassificationTests        - D6 classification on canned diffs, using the REAL
                                     loaded docs/autorepair/ policy (is_forbidden() only
                                     matches path STRINGS, so pointing it at a throwaway
                                     fixture repo whose paths mirror the real repo's
                                     layout is a faithful test of the real law): an added
                                     tests/ file -> testAdds; a modified existing tests/
                                     file -> forbidden; a modified docs/autorepair/ law
                                     file -> forbidden; a modified native/engine/*.cpp
                                     file -> production; and all four in one mixed patch.
  DriftDetectorTests              - the tripwire demonstrated firing (both directions),
                                     done safely in a FIXTURE "main" repo, never the real
                                     one: an injected untracked file, and a mutated
                                     tracked file, each turn main_drift_check() red;
                                     restoring turns it green again.
"""
from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "sandbox.py"


def _load_sandbox_module():
    spec = importlib.util.spec_from_file_location("autorepair_sandbox", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_sandbox_module()


# ── git plumbing (mirrors tests/test_precommit_coverage_dispatch.py's _git) ────


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


def _make_main_fixture_repo(root: Path) -> tuple[str, str]:
    """A tiny two-commit fixture 'main' repo. Returns (first_sha, second_sha) so a test
    can prove create() checked out the FIRST commit specifically, not just current HEAD
    (which is the second commit, with different file content)."""
    _init_fixture_repo(root)
    _write(root / "native" / "engine" / "Foo.cpp", "// v1 - first commit\n")
    _write(root / "tests" / "foo.py", "# v1 - first commit\n")
    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "first commit")
    first_sha = _git(root, "rev-parse", "HEAD").stdout.strip()

    _write(root / "native" / "engine" / "Foo.cpp", "// v2 - second commit\n")
    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "second commit")
    second_sha = _git(root, "rev-parse", "HEAD").stdout.strip()

    return first_sha, second_sha


# ══════════════════════════════════════════════════════════════════════════
# create()
# ══════════════════════════════════════════════════════════════════════════


class SandboxCreateTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self._tmp.name)
        self.main_repo = self.tmp_path / "main"
        self.main_repo.mkdir()
        self.first_sha, self.second_sha = _make_main_fixture_repo(self.main_repo)
        self.sandbox_root = self.tmp_path / "sbx-root"

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_create_clone_has_no_origin_remote(self):
        clone = mod.create(
            self.first_sha, "AR-test-0001", main_repo=self.main_repo, sandbox_root=self.sandbox_root
        )
        self.assertTrue(clone.exists())
        self.assertTrue((clone / ".git").exists(), "clone must be a real .git repo, not a bare copy")
        remotes = _git(clone, "remote").stdout.strip()
        self.assertEqual(remotes, "", "sandbox clone must have zero git remotes after create()")

    def test_create_checks_out_the_exact_requested_sha(self):
        clone = mod.create(
            self.first_sha, "AR-test-0002", main_repo=self.main_repo, sandbox_root=self.sandbox_root
        )
        head = _git(clone, "rev-parse", "HEAD").stdout.strip()
        self.assertEqual(head, self.first_sha)
        self.assertNotEqual(
            head, self.second_sha, "sanity: the two fixture commits must actually differ"
        )
        content = (clone / "native" / "engine" / "Foo.cpp").read_text(encoding="utf-8")
        self.assertIn("v1", content, "checkout must land on the FIRST commit's content, not HEAD")

    def test_create_refuses_to_reuse_an_existing_clone_path(self):
        mod.create(self.first_sha, "AR-test-0003", main_repo=self.main_repo, sandbox_root=self.sandbox_root)
        with self.assertRaises(mod.SandboxError):
            mod.create(
                self.second_sha, "AR-test-0003", main_repo=self.main_repo, sandbox_root=self.sandbox_root
            )


# ══════════════════════════════════════════════════════════════════════════
# Negative control (mandatory, both directions): origin-removal assertion
# ══════════════════════════════════════════════════════════════════════════


class OriginRemovalNegativeControlTests(unittest.TestCase):
    """D5/A1's refusal law: 'hand create() a sandbox where origin removal is skipped ->
    the origin-present assertion refuses to proceed.' Exercised directly against
    sandbox._assert_no_remotes() - the identical guard create() itself calls - rather
    than forking create()'s control flow just to inject a skip."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self._tmp.name)
        self.main_repo = self.tmp_path / "main"
        self.main_repo.mkdir()
        self.first_sha, _ = _make_main_fixture_repo(self.main_repo)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_origin_present_refuses_then_removed_succeeds(self):
        clone = self.tmp_path / "manual-clone"
        _git(self.tmp_path, "clone", "--local", "--no-hardlinks", str(self.main_repo), str(clone))
        _git(clone, "checkout", self.first_sha)

        # Origin removal DELIBERATELY SKIPPED here - simulates the exact failure mode
        # the assertion exists to catch.
        remotes_before = _git(clone, "remote").stdout.strip()
        self.assertEqual(
            remotes_before, "origin", "fixture sanity: origin must be present before the assertion runs"
        )

        with self.assertRaises(mod.OriginNotRemovedError) as ctx:
            mod._assert_no_remotes(clone)
        self.assertIn(str(clone), str(ctx.exception))
        self.assertIn("origin", str(ctx.exception))

        # Now perform the removal exactly as create() would, and confirm the SAME
        # assertion function passes clean - both directions on one code path.
        _git(clone, "remote", "remove", "origin")
        mod._assert_no_remotes(clone)  # must not raise


# ══════════════════════════════════════════════════════════════════════════
# extract_patch() - D6 classification, real loaded policy
# ══════════════════════════════════════════════════════════════════════════


class PatchClassificationTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "sandbox"
        self.root.mkdir()
        _init_fixture_repo(self.root)
        _write(self.root / "native" / "engine" / "Foo.cpp", "// v1 - production file\n")
        _write(self.root / "tests" / "foo.py", "# v1 - existing test\n")
        _write(self.root / "docs" / "autorepair" / "policy.json", '{"schema": 1}\n')
        _git(self.root, "add", "-A")
        _git(self.root, "commit", "-q", "-m", "seed classification fixture")

        # The REAL, shipped Guardian Loop law - is_forbidden() matches path STRINGS
        # only, so pointing it at this throwaway fixture repo (whose paths mirror the
        # real repo's own layout) is a faithful test of the real, live law - not a
        # reinvented approximation of it.
        self.policy = mod.load_policy()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_added_test_file_is_a_test_add(self):
        _write(self.root / "tests" / "test_new_bug.py", "# added by repair\n")
        _git(self.root, "add", "-A")
        patch = mod.extract_patch(self.root, self.policy)
        self.assertIn("tests/test_new_bug.py", patch["testAdds"])
        self.assertEqual(patch["forbidden"], [])
        self.assertEqual(patch["production"], [])

    def test_modified_existing_test_file_is_forbidden(self):
        _write(self.root / "tests" / "foo.py", "# v2 - repair patch tries to edit an existing test\n")
        _git(self.root, "add", "-A")
        patch = mod.extract_patch(self.root, self.policy)
        self.assertIn("tests/foo.py", patch["forbidden"])
        self.assertNotIn("tests/foo.py", patch["production"])
        self.assertNotIn("tests/foo.py", patch["testAdds"])

    def test_modified_law_file_is_forbidden(self):
        _write(self.root / "docs" / "autorepair" / "policy.json", '{"schema": 1, "tampered": true}\n')
        _git(self.root, "add", "-A")
        patch = mod.extract_patch(self.root, self.policy)
        self.assertIn("docs/autorepair/policy.json", patch["forbidden"])

    def test_modified_production_cpp_is_production(self):
        _write(self.root / "native" / "engine" / "Foo.cpp", "// v2 - the actual fix\n")
        _git(self.root, "add", "-A")
        patch = mod.extract_patch(self.root, self.policy)
        self.assertIn("native/engine/Foo.cpp", patch["production"])
        self.assertEqual(patch["forbidden"], [])
        self.assertEqual(patch["testAdds"], [])

    def test_full_mixed_patch_classifies_every_bucket_at_once(self):
        _write(self.root / "native" / "engine" / "Foo.cpp", "// v2 fix\n")
        _write(self.root / "tests" / "foo.py", "# tampered\n")
        _write(self.root / "docs" / "autorepair" / "policy.json", '{"tampered": true}\n')
        _write(self.root / "tests" / "test_new_bug.py", "# new bug test\n")
        _git(self.root, "add", "-A")

        patch = mod.extract_patch(self.root, self.policy)

        self.assertEqual(sorted(patch["production"]), ["native/engine/Foo.cpp"])
        self.assertEqual(sorted(patch["testAdds"]), ["tests/test_new_bug.py"])
        self.assertEqual(sorted(patch["forbidden"]), ["docs/autorepair/policy.json", "tests/foo.py"])

    def test_add_outside_tests_that_is_forbidden_by_law_is_forbidden_not_production(self):
        """A repair agent dropping a brand-new file straight into docs/autorepair/ (no
        addExempt entry covers that directory - only tests/** does) must be caught as
        forbidden, not waved through as an ordinary production add."""
        _write(self.root / "docs" / "autorepair" / "new-law.json", "{}\n")
        _git(self.root, "add", "-A")
        patch = mod.extract_patch(self.root, self.policy)
        self.assertIn("docs/autorepair/new-law.json", patch["forbidden"])
        self.assertEqual(patch["production"], [])


# ══════════════════════════════════════════════════════════════════════════
# Drift tripwire (Program ruling 7b) - fires both directions, fixture-only
# ══════════════════════════════════════════════════════════════════════════


class DriftDetectorTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.fake_main = Path(self._tmp.name) / "fake-main"
        self.fake_main.mkdir()
        _init_fixture_repo(self.fake_main)
        _write(self.fake_main / "README.txt", "hello\n")
        _git(self.fake_main, "add", "-A")
        _git(self.fake_main, "commit", "-q", "-m", "seed")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_drift_check_is_clean_when_nothing_changed(self):
        before = mod.main_drift_snapshot(self.fake_main)
        mod.main_drift_check(before)  # must not raise

    def test_drift_check_fires_on_injected_untracked_file(self):
        """The tripwire demonstrated firing: an untracked file appears in the fixture
        'main' repo (standing in for something a sandbox stage leaked) - main_drift_check
        must raise DriftViolation naming the file. Restoring turns it clean again."""
        before = mod.main_drift_snapshot(self.fake_main)
        leaked = self.fake_main / "leaked-from-sandbox.txt"
        _write(leaked, "should never appear in MAIN\n")

        with self.assertRaises(mod.DriftViolation) as ctx:
            mod.main_drift_check(before)
        self.assertIn("leaked-from-sandbox.txt", str(ctx.exception))

        leaked.unlink()
        mod.main_drift_check(before)  # restored -> clean again, both directions proven

    def test_drift_check_fires_on_modified_tracked_file(self):
        before = mod.main_drift_snapshot(self.fake_main)
        readme = self.fake_main / "README.txt"
        original = readme.read_text(encoding="utf-8")
        _write(readme, "mutated - simulating a leaked edit\n")

        with self.assertRaises(mod.DriftViolation):
            mod.main_drift_check(before)

        # Restore byte-for-byte: _write() (like the module's own fixtures) pins
        # newline="\n" - a plain write_text() here would let Windows text-mode
        # LF->CRLF translation silently reintroduce drift on the "restored" write.
        _write(readme, original)
        mod.main_drift_check(before)


if __name__ == "__main__":
    print(f"[test_autorepair_sandbox] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)

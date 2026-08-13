#!/usr/bin/env python3
"""test_precommit_coverage_dispatch.py - tests for scripts/precommit-encyclopedia-check.sh's
Lanista coverage dispatch (Coverage Slice 2).

docs/superpowers/plans/2026-08-13-colosseum-lanista-coverage-ledger-plan.md, Slice 2
("Existing pre-commit drift integration"). Pure Python, stdlib unittest, matching the
Slice 1 convention in tests/test_lanista_coverage.py - runnable directly:

    python tests/test_precommit_coverage_dispatch.py

Every test builds a THROWAWAY hermetic git repository under a tempdir and invokes the
REAL hook script (`sh scripts/precommit-encyclopedia-check.sh`) against real `git add`
staging - never the real Colosseum repo, never `.git/hooks/pre-commit` in this repo, and
never a mock of the staged-path-intersection rule. The hook script, scripts/lanista_coverage.py
and scripts/code_encyclopedia.py are copied byte-for-byte from this repo into each fixture
repo so the exact production dispatch logic is what runs.

Fixture shape (built by `_make_fixture_repo`):
  - docs/lanista-coverage/ledger.json with two families: "widget-family" (watched files:
    fixtures/widget/Widget.qml, fixtures/widget/evidence.md) and "sibling-family"
    (fixtures/sibling/Sibling.qml, fixtures/sibling/evidence.md) - a second family exists
    only to prove drift reporting names the right family and that the dispatch trigger
    genuinely depends on staged-path intersection, not "any coverage file exists".
  - docs/encyclopedia/sample.paths watching fixtures/encyclopedia/Sample.qml - a real
    encyclopedia-covered file, present so the "encyclopedia-only drift still blocks"
    regression can be proven on the SAME hook invocation path as coverage.
  - fixtures/unrelated/Unrelated.qml - a plain QML file in neither manifest.
Both families are bootstrapped CURRENT (accepted) before the baseline commit.

Test groups:
  ProofMatrixTests      - the plan's required 8-row matrix: no overlap, watched+CURRENT,
                           watched+DRIFTED, ledger/manifest staged without acceptance,
                           re-accept, unrelated QML, encyclopedia-only regression, both clean.
  NegativeControlTests   - NC2 (mandatory): stage exactly one watched fixture dependency
                           without accepting -> red; restore/re-accept -> green. Both
                           directions asserted in one test, both outputs captured.
  FailClosedTests        - deleted watched file, renamed watched file.
  CloneShapeTests         - a clone with no docs/lanista-coverage/ is a clean no-op.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HOOK_PATH = REPO_ROOT / "scripts" / "precommit-encyclopedia-check.sh"
COVERAGE_SCRIPT = REPO_ROOT / "scripts" / "lanista_coverage.py"
ENCYCLOPEDIA_SCRIPT = REPO_ROOT / "scripts" / "code_encyclopedia.py"


# ── git / hook plumbing ───────────────────────────────────────────────────────

def _git(cwd: Path, *args: str) -> subprocess.CompletedProcess:
    result = subprocess.run(
        ["git", *args], cwd=str(cwd), capture_output=True, text=True
    )
    if result.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed: {result.stderr}")
    return result


def _run_python(cwd: Path, script: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(script), *args],
        cwd=str(cwd), capture_output=True, text=True,
    )


def _run_hook(cwd: Path) -> subprocess.CompletedProcess:
    """Invokes the REAL hook script directly (never via .git/hooks/pre-commit, and
    never against the real Colosseum repo) exactly as a pre-commit hook would see it:
    cwd inside the repo, whatever is currently staged in the index."""
    return subprocess.run(
        ["sh", str(cwd / "scripts" / "precommit-encyclopedia-check.sh")],
        cwd=str(cwd), capture_output=True, text=True,
    )


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


# ── fixture ledger content ────────────────────────────────────────────────────

def _surface(surface_id: str, family: str, evidence: str) -> dict:
    return {
        "id": surface_id,
        "family": family,
        "state": "covered",
        "target": {"kind": "objectName", "value": surface_id},
        "capabilities": {"actions": ["ui-click"], "observations": []},
        "evidence": [evidence],
        "missingCapability": None,
        "rationale": None,
    }


def _ledger_json() -> str:
    ledger = {
        "schema": 1,
        "surfaces": [
            _surface("widgetPrimary", "widget-family", "fixtures/widget/evidence.md"),
            _surface("siblingPrimary", "sibling-family", "fixtures/sibling/evidence.md"),
        ],
    }
    return json.dumps(ledger, indent=2, ensure_ascii=False) + "\n"


def _make_fixture_repo(root: Path) -> None:
    """Seeds a throwaway git repo with two synthetic coverage families (both accepted,
    both CURRENT), one encyclopedia-watched fixture (accepted, CURRENT), and one QML
    file outside every manifest. Never touches the real Colosseum repo."""
    _git(root, "init", "-q")
    _git(root, "config", "user.email", "test@example.com")
    _git(root, "config", "user.name", "Fixture Author")

    # Copy the real production scripts byte-for-byte - this proof exercises the actual
    # dispatch logic, not a re-derived approximation of it.
    (root / "scripts").mkdir(parents=True)
    shutil.copy(HOOK_PATH, root / "scripts" / "precommit-encyclopedia-check.sh")
    shutil.copy(COVERAGE_SCRIPT, root / "scripts" / "lanista_coverage.py")
    shutil.copy(ENCYCLOPEDIA_SCRIPT, root / "scripts" / "code_encyclopedia.py")

    _write(root / "docs" / "lanista-coverage" / "ledger.json", _ledger_json())
    _write(
        root / "docs" / "lanista-coverage" / "widget-family.paths",
        "# widget-family fixture manifest\n"
        "fixtures/widget/Widget.qml\n"
        "fixtures/widget/evidence.md\n",
    )
    _write(
        root / "docs" / "lanista-coverage" / "sibling-family.paths",
        "# sibling-family fixture manifest\n"
        "fixtures/sibling/Sibling.qml\n"
        "fixtures/sibling/evidence.md\n",
    )
    _write(
        root / "fixtures" / "widget" / "Widget.qml",
        "// widget qml fixture v1 - test-only, not real product source.\n"
        "import QtQuick\n\nItem { id: widgetPrimary }\n",
    )
    _write(root / "fixtures" / "widget" / "evidence.md", "# Widget evidence\n\nFixture only.\n")
    _write(
        root / "fixtures" / "sibling" / "Sibling.qml",
        "// sibling qml fixture v1 - test-only, not real product source.\n"
        "import QtQuick\n\nItem { id: siblingPrimary }\n",
    )
    _write(root / "fixtures" / "sibling" / "evidence.md", "# Sibling evidence\n\nFixture only.\n")
    _write(
        root / "fixtures" / "unrelated" / "Unrelated.qml",
        "// standalone unrelated qml - not referenced by any manifest.\n"
        "import QtQuick\n\nItem {}\n",
    )

    _write(
        root / "docs" / "encyclopedia" / "sample.paths",
        "# sample fixture guide manifest\nfixtures/encyclopedia/Sample.qml\n",
    )
    _write(
        root / "fixtures" / "encyclopedia" / "Sample.qml",
        "// Sample encyclopedia fixture - test-only source, not real product code.\n"
        "import QtQuick\n\nItem { id: sampleFixture }\n",
    )

    # Both acceptance tools resolve `git rev-parse HEAD` for provenance, so an initial
    # commit must exist before either bootstrap acceptance can run.
    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "seed fixture repo: two coverage families + one guide (unaccepted)")

    # Bootstrap: accept both coverage families, bootstrap the encyclopedia guide.
    accept = _run_python(root, root / "scripts" / "lanista_coverage.py",
                          "--accept-all-drifted", "--accepted-by", "Fixture Harness")
    assert accept.returncode == 0, accept.stderr
    ency = _run_python(
        root, root / "scripts" / "code_encyclopedia.py",
        "--paths", "docs/encyclopedia/sample.paths",
        "--output", "docs/encyclopedia/sample-index.md",
        "--state", "docs/encyclopedia/sample-state.json",
    )
    assert ency.returncode == 0, ency.stderr

    _git(root, "add", "-A")
    _git(root, "commit", "-q", "-m", "accept baseline: both coverage families + guide CURRENT")


def _accept_family(root: Path, family: str) -> subprocess.CompletedProcess:
    return _run_python(root, root / "scripts" / "lanista_coverage.py",
                        "--accept", family, "--accepted-by", "Fixture Harness")


class HookTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        _make_fixture_repo(self.root)

    def tearDown(self) -> None:
        self._tmp.cleanup()


# ══════════════════════════════════════════════════════════════════════════
# Required proof matrix (all 8 rows)
# ══════════════════════════════════════════════════════════════════════════

class ProofMatrixTests(HookTestCase):
    def test_row1_no_overlap_with_any_manifest_is_neutral_noop(self):
        """No staged path intersects any coverage or encyclopedia manifest - the hook
        must not even invoke either checker."""
        _write(
            self.root / "fixtures" / "unrelated" / "Unrelated.qml",
            "// standalone unrelated qml - EDITED, still not referenced by any manifest.\n"
            "import QtQuick\n\nItem {}\n",
        )
        _git(self.root, "add", "fixtures/unrelated/Unrelated.qml")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Lanista coverage", result.stdout)
        self.assertNotIn("encyclopedia drift", result.stdout)

    def test_row2_watched_file_staged_family_current_is_green(self):
        """Real workflow: edit a watched file, re-accept its family, stage the edit
        together with the regenerated ledger.json/accepted-state.json, and confirm the
        hook passes even though watched files (and the global ledger/state files) are
        all staged - because the edit was properly accepted before commit."""
        _write(
            self.root / "fixtures" / "widget" / "Widget.qml",
            "// widget qml fixture v2 - test-only, not real product source.\n"
            "import QtQuick\n\nItem { id: widgetPrimary; visible: true }\n",
        )
        accept = _accept_family(self.root, "widget-family")
        self.assertEqual(accept.returncode, 0, accept.stderr)
        _git(self.root, "add", "-A")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Lanista coverage drift", result.stdout)

    def test_row3_watched_file_staged_family_drifted_is_red(self):
        """Edit a watched file WITHOUT accepting, stage only that file - the hook must
        block, and the message must name widget-family."""
        _write(
            self.root / "fixtures" / "widget" / "Widget.qml",
            "// widget qml fixture v2 - UNACCEPTED CHANGE.\n"
            "import QtQuick\n\nItem { id: widgetPrimary; visible: false }\n",
        )
        _git(self.root, "add", "fixtures/widget/Widget.qml")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage drift", result.stdout)
        self.assertIn("DRIFTED widget-family", result.stdout)
        self.assertIn("not only the file(s) you", result.stdout)  # names the whole-family note

    def test_row4_ledger_staged_without_acceptance_is_red(self):
        """A ledger-only edit (a claim change) staged WITHOUT re-accepting must drift -
        proves the 'ledger.json ... staged' leg of D4's trigger, and that ledger.json
        alone (no watched-file edit at all) is enough to open the gate."""
        ledger_path = self.root / "docs" / "lanista-coverage" / "ledger.json"
        ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
        for surface in ledger["surfaces"]:
            if surface["family"] == "widget-family":
                surface["rationale"] = "Updated claim text, not yet reviewed/accepted."
        ledger_path.write_text(json.dumps(ledger, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        _git(self.root, "add", "docs/lanista-coverage/ledger.json")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage drift", result.stdout)
        self.assertIn("DRIFTED widget-family", result.stdout)
        self.assertIn("family ledger digest changed", result.stdout)

    def test_row4b_paths_manifest_staged_without_acceptance_is_red(self):
        """The 'that family's .paths ... staged' leg of D4's trigger: add a new watched
        dependency to widget-family.paths (a structural manifest change) without
        accepting - drift via 'watched path(s) added', not a blob change."""
        manifest_path = self.root / "docs" / "lanista-coverage" / "widget-family.paths"
        _write(self.root / "fixtures" / "widget" / "extra.md", "# Extra widget evidence\n")
        manifest_path.write_text(
            manifest_path.read_text(encoding="utf-8") + "fixtures/widget/extra.md\n",
            encoding="utf-8",
        )
        _git(self.root, "add", "docs/lanista-coverage/widget-family.paths", "fixtures/widget/extra.md")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("DRIFTED widget-family", result.stdout)
        self.assertIn("watched path(s) added", result.stdout)

    def test_row5_reaccept_after_drift_is_green(self):
        """Continuation of row 3: after the block, accept widget-family and stage the
        regenerated ledger/state alongside the edit - must turn green."""
        _write(
            self.root / "fixtures" / "widget" / "Widget.qml",
            "// widget qml fixture v2 - about to be accepted.\n"
            "import QtQuick\n\nItem { id: widgetPrimary; visible: false }\n",
        )
        _git(self.root, "add", "fixtures/widget/Widget.qml")
        red = _run_hook(self.root)
        self.assertEqual(red.returncode, 1, red.stdout + red.stderr)

        accept = _accept_family(self.root, "widget-family")
        self.assertEqual(accept.returncode, 0, accept.stderr)
        _git(self.root, "add", "-A")

        green = _run_hook(self.root)
        self.assertEqual(green.returncode, 0, green.stdout + green.stderr)
        self.assertNotIn("Lanista coverage drift", green.stdout)

    def test_row6_unrelated_qml_outside_manifests_is_green(self):
        """A brand-new QML file, staged, that appears in no manifest at all - must not
        trigger coverage OR encyclopedia checking."""
        _write(
            self.root / "fixtures" / "unrelated" / "BrandNew.qml",
            "// brand new file, in no manifest.\nimport QtQuick\n\nItem {}\n",
        )
        _git(self.root, "add", "fixtures/unrelated/BrandNew.qml")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Lanista coverage", result.stdout)
        self.assertNotIn("encyclopedia drift", result.stdout)

    def test_row7_encyclopedia_only_drift_still_blocks_non_negotiable(self):
        """Non-negotiable regression: coverage dispatch must not weaken or replace the
        existing encyclopedia block. Drift only the encyclopedia-watched fixture (no
        coverage family touched at all) and confirm it still blocks exactly as before."""
        _write(
            self.root / "fixtures" / "encyclopedia" / "Sample.qml",
            "// Sample encyclopedia fixture - UNACCEPTED CHANGE.\n"
            "import QtQuick\n\nItem { id: sampleFixture; visible: false }\n",
        )
        _git(self.root, "add", "fixtures/encyclopedia/Sample.qml")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("encyclopedia drift", result.stdout)
        self.assertNotIn("Lanista coverage drift", result.stdout)

    def test_row8_both_clean_together_is_green(self):
        """A single commit that legitimately touches BOTH a coverage-watched file and
        an encyclopedia-watched file, each properly re-accepted, must pass green -
        proves the two independent gates coexist without interfering."""
        _write(
            self.root / "fixtures" / "widget" / "Widget.qml",
            "// widget qml fixture v3 - properly accepted alongside an encyclopedia edit.\n"
            "import QtQuick\n\nItem { id: widgetPrimary; opacity: 1.0 }\n",
        )
        accept = _accept_family(self.root, "widget-family")
        self.assertEqual(accept.returncode, 0, accept.stderr)

        _write(
            self.root / "fixtures" / "encyclopedia" / "Sample.qml",
            "// Sample encyclopedia fixture - properly re-accepted alongside a coverage edit.\n"
            "import QtQuick\n\nItem { id: sampleFixture; opacity: 1.0 }\n",
        )
        ency = _run_python(
            self.root, self.root / "scripts" / "code_encyclopedia.py",
            "--paths", "docs/encyclopedia/sample.paths",
            "--output", "docs/encyclopedia/sample-index.md",
            "--state", "docs/encyclopedia/sample-state.json",
            "--accept-all-drifted",
        )
        self.assertEqual(ency.returncode, 0, ency.stderr)

        _git(self.root, "add", "-A")
        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Lanista coverage drift", result.stdout)
        self.assertNotIn("encyclopedia drift", result.stdout)


# ══════════════════════════════════════════════════════════════════════════
# NC2 (mandatory negative control)
# ══════════════════════════════════════════════════════════════════════════

class NegativeControlTests(HookTestCase):
    def test_nc2_watched_blob_mutation_without_accept_turns_red_then_green(self):
        """NC2: stage exactly one watched fixture dependency without accepting -> the
        coverage check turns red; restore (re-accept) -> green. Both outputs captured
        on self for the report."""
        _write(
            self.root / "fixtures" / "sibling" / "evidence.md",
            "# Sibling evidence\n\nMUTATED - not yet accepted.\n",
        )
        _git(self.root, "add", "fixtures/sibling/evidence.md")

        red = _run_hook(self.root)
        self.nc2_red_output = red.stdout + red.stderr
        self.assertEqual(red.returncode, 1, self.nc2_red_output)
        self.assertIn("DRIFTED sibling-family", red.stdout)
        self.assertIn("watched blob(s) changed", red.stdout)

        accept = _accept_family(self.root, "sibling-family")
        self.assertEqual(accept.returncode, 0, accept.stderr)
        _git(self.root, "add", "-A")

        green = _run_hook(self.root)
        self.nc2_green_output = green.stdout + green.stderr
        self.assertEqual(green.returncode, 0, self.nc2_green_output)
        self.assertNotIn("Lanista coverage drift", green.stdout)


# ══════════════════════════════════════════════════════════════════════════
# Fail-closed: deleted / renamed watched files
# ══════════════════════════════════════════════════════════════════════════

class FailClosedTests(HookTestCase):
    def test_deleted_watched_file_fails_closed(self):
        """Delete a watched file, stage the deletion (git rm) - the coverage checker
        cannot even build current state (missing watched source), so this must block
        with a SCHEMA ERROR, not silently pass or crash uncontrolled."""
        _git(self.root, "rm", "-q", "fixtures/widget/evidence.md")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage ledger check could not run", result.stdout)
        self.assertIn("missing watched source", result.stdout)

    def test_renamed_watched_file_fails_closed(self):
        """git mv a watched file to an unwatched path - old path is now missing from
        disk (manifest still names it), new path is untracked by any manifest. Must
        fail closed the same way a deletion does, not silently treat the rename as
        clean."""
        _git(self.root, "mv", "fixtures/widget/Widget.qml", "fixtures/widget/WidgetRenamed.qml")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage ledger check could not run", result.stdout)
        self.assertIn("missing watched source", result.stdout)


# ══════════════════════════════════════════════════════════════════════════
# Clone shape: no docs/lanista-coverage/ at all
# ══════════════════════════════════════════════════════════════════════════

class CloneShapeTests(unittest.TestCase):
    def test_clone_without_lanista_coverage_dir_is_clean_noop(self):
        """A fresh clone that predates the coverage ledger (no docs/lanista-coverage/
        at all) must be a clean no-op for coverage - proving the encyclopedia guard and
        the coverage guard are independently scoped, not coupled through one early
        `exit 0`. The encyclopedia's own behavior (accept + drift-block) must still work
        unaffected in this shape."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _git(root, "init", "-q")
            _git(root, "config", "user.email", "test@example.com")
            _git(root, "config", "user.name", "Fixture Author")

            (root / "scripts").mkdir(parents=True)
            shutil.copy(HOOK_PATH, root / "scripts" / "precommit-encyclopedia-check.sh")
            shutil.copy(ENCYCLOPEDIA_SCRIPT, root / "scripts" / "code_encyclopedia.py")
            # Deliberately NOT copying lanista_coverage.py and NOT creating
            # docs/lanista-coverage/ at all - this is the pre-coverage clone shape.

            _write(
                root / "docs" / "encyclopedia" / "sample.paths",
                "fixtures/encyclopedia/Sample.qml\n",
            )
            _write(
                root / "fixtures" / "encyclopedia" / "Sample.qml",
                "// Sample encyclopedia fixture - test-only source.\n"
                "import QtQuick\n\nItem {}\n",
            )
            ency = _run_python(
                root, root / "scripts" / "code_encyclopedia.py",
                "--paths", "docs/encyclopedia/sample.paths",
                "--output", "docs/encyclopedia/sample-index.md",
                "--state", "docs/encyclopedia/sample-state.json",
            )
            self.assertEqual(ency.returncode, 0, ency.stderr)
            _git(root, "add", "-A")
            _git(root, "commit", "-q", "-m", "seed pre-coverage clone shape")

            # A totally unrelated staged file: whole hook must no-op.
            _write(root / "fixtures" / "misc.txt", "misc\n")
            _git(root, "add", "fixtures/misc.txt")
            result = _run_hook(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("Lanista coverage", result.stdout)

            # Encyclopedia drift must still independently block in this exact shape.
            _write(
                root / "fixtures" / "encyclopedia" / "Sample.qml",
                "// Sample encyclopedia fixture - UNACCEPTED CHANGE.\n"
                "import QtQuick\n\nItem { visible: false }\n",
            )
            _git(root, "add", "fixtures/encyclopedia/Sample.qml")
            drifted = _run_hook(root)
            self.assertEqual(drifted.returncode, 1, drifted.stdout + drifted.stderr)
            self.assertIn("encyclopedia drift", drifted.stdout)


# ══════════════════════════════════════════════════════════════════════════
# Corrupted accepted-state.json — field finding #1 from Agent 0's review gate:
# load_state() runs before mode branching, so --accept cannot self-heal a corrupted or
# hand-edited accepted-state.json. Inside a pre-commit hook that would otherwise read as
# an unfixable block, so the hook's own output must name the recovery path (delete +
# --accept-all-drifted) rather than just surfacing the raw schema error.
# ══════════════════════════════════════════════════════════════════════════

class CorruptedStateMessagingTests(HookTestCase):
    def test_hand_edited_accepted_state_names_recovery_path(self):
        state_path = self.root / "docs" / "lanista-coverage" / "accepted-state.json"
        state = json.loads(state_path.read_text(encoding="utf-8"))
        # Hand-edit one accepted blob hash without recomputing the integrity digest -
        # exactly the "do not hand-edit generated state" trap load_state() guards.
        first_family = next(iter(state["families"]))
        first_path = next(iter(state["families"][first_family]["acceptedBlobs"]))
        state["families"][first_family]["acceptedBlobs"][first_path] = "f" * 40
        state_path.write_text(json.dumps(state, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
                               encoding="utf-8")
        _git(self.root, "add", "docs/lanista-coverage/accepted-state.json")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage ledger check could not run", result.stdout)
        self.assertIn("integrity mismatch", result.stdout)
        self.assertIn("corrupted or hand-edited", result.stdout)
        self.assertIn("CANNOT self-heal", result.stdout)
        self.assertIn("delete", result.stdout)
        self.assertIn("--accept-all-drifted", result.stdout)

        # Confirm --accept truly cannot self-heal it (the exact claim the message makes).
        cannot_heal = _accept_family(self.root, "widget-family")
        self.assertNotEqual(cannot_heal.returncode, 0)
        self.assertIn("integrity mismatch", cannot_heal.stderr)

        # Confirm the documented recovery actually works.
        state_path.unlink()
        recovered = _run_python(self.root, self.root / "scripts" / "lanista_coverage.py",
                                 "--accept-all-drifted", "--accepted-by", "Fixture Harness")
        self.assertEqual(recovered.returncode, 0, recovered.stderr)

    def test_ordinary_ledger_schema_error_gets_generic_message_not_state_recovery_text(self):
        """A schema error that has NOTHING to do with accepted-state.json (a bad ledger.json)
        must not print the accepted-state-specific recovery text - the message should be
        specific to what's actually broken."""
        ledger_path = self.root / "docs" / "lanista-coverage" / "ledger.json"
        mutated = ledger_path.read_text(encoding="utf-8").replace('"state": "covered"', '"state": "coveredd"', 1)
        ledger_path.write_text(mutated, encoding="utf-8")
        _git(self.root, "add", "docs/lanista-coverage/ledger.json")

        result = _run_hook(self.root)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Lanista coverage ledger check could not run", result.stdout)
        self.assertIn("unknown state", result.stdout)
        self.assertNotIn("corrupted or hand-edited", result.stdout)
        self.assertNotIn("CANNOT self-heal", result.stdout)


if __name__ == "__main__":
    print(f"[test_precommit_coverage_dispatch] hook under test: {HOOK_PATH}")
    unittest.main(verbosity=2)

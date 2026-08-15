#!/usr/bin/env python3
"""test_autorepair_bugdoc.py - tests for scripts/autorepair/bugdoc.py
(document-only autonomy's bug.md capstone).

docs/superpowers/plans/2026-08-15-colosseum-night-watch-n0-plan.md. Pure Python,
stdlib unittest, house flat convention. Runnable directly:

    python tests/test_autorepair_bugdoc.py -v

HERMETIC: no I/O except tempfile dirs for write_bug_doc(); render_bug_doc() is a
pure function over hand-built dicts shaped like the real incident.json /
triage.json / diagnosis.json records (field names pinned against the D10
rehearsal's real artifacts, artifacts/autorepair/AR-2026-08-15-0001/).
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "bugdoc.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_module("autorepair_bugdoc", SCRIPT_PATH)

WHEN = datetime(2026, 8, 15, 21, 0, 0, tzinfo=timezone.utc)


def _incident() -> dict:
    return {
        "schema": 1,
        "id": "AR-test-0001",
        "baseSha": "deadbeef",
        "scenario": "tests/lanista_scenarios/journey_open_manga.json",
        "scenarioName": "journey-open-manga",
        "createdAt": "2026-08-15T13:51:53+00:00",
        "fingerprint": "abc123",
        "failingStep": {
            "index": 14, "label": "readerReady is the real false->true transition",
            "status": "FAIL", "detail": "matched == true - got undefined",
            "expected": "matched == true", "got": "undefined",
        },
    }


def _triage() -> dict:
    return {
        "verdict": "CONFIRMED",
        "reproduced": True,
        "failingStepConsistency": {
            "applicable": True, "label": "readerReady wait",
            "count": 3, "ofFailures": 3, "totalRuns": 3, "confirmThreshold": 2,
        },
    }


def _diagnosis() -> dict:
    return {
        "observed": "the readiness wait times out one step after the reader shows",
        "expected": "readerReady fires when the current page is genuinely rendered",
        "rootCause": {
            "file": "qml/comicreader/ComicReaderShell.qml", "line": 208,
            "claim": "the conjunction demands an unsatisfiable equality",
        },
        "seam": "the reader readiness contract",
        "confidence": "high",
        "proposedRepair": "restore the plain equality",
        "wouldNeedForbiddenChange": False,
    }


class RenderBugDocTests(unittest.TestCase):
    def test_happy_path_carries_every_section(self):
        body = mod.render_bug_doc(_incident(), _triage(), _diagnosis(), generated_at=WHEN)
        for needle in (
            "# Bug - AR-test-0001",
            "DOCUMENTED ONLY",
            "Triage verdict CONFIRMED",
            "3 of 3 runs",
            "qml/comicreader/ComicReaderShell.qml:208",
            "## Proposed repair (NOT applied)",
            "diagnosis.json",
        ):
            self.assertIn(needle, body)
        self.assertNotIn("<not recorded>", body)

    def test_documented_status_names_the_mode(self):
        """The status line must say WHY nothing was fixed - a bug.md that could be
        mistaken for a fix report is the failure this mode exists to prevent."""
        body = mod.render_bug_doc(_incident(), _triage(), _diagnosis(), generated_at=WHEN)
        self.assertIn("document-only", body)
        self.assertIn("no repair was attempted", body)

    def test_missing_fields_degrade_to_not_recorded_never_crash(self):
        body = mod.render_bug_doc({}, {}, {}, generated_at=WHEN)
        self.assertIn("# Bug - <not recorded>", body)
        self.assertGreaterEqual(body.count("<not recorded>"), 5)

    def test_emoji_is_refused(self):
        poisoned = _diagnosis()
        poisoned["proposedRepair"] = "just flip the flag \U0001F389"
        with self.assertRaises(ValueError):
            mod.render_bug_doc(_incident(), _triage(), poisoned, generated_at=WHEN)


class WriteBugDocTests(unittest.TestCase):
    def test_write_places_bug_md_next_to_stage_files(self):
        with tempfile.TemporaryDirectory(prefix="bugdoc_") as tmp:
            d = Path(tmp)
            path = mod.write_bug_doc(
                d, _incident(), _triage(), _diagnosis(), generated_at=WHEN,
            )
            self.assertEqual(path, d / "bug.md")
            self.assertTrue(path.is_file())
            self.assertIn("# Bug - AR-test-0001", path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""test_soak_digest.py — tests for scripts/soak-digest.py (Slice S1-Digest).

Slice S1-Digest, docs/superpowers/plans/2026-08-13-colosseum-visibility-phase2-plan.md.
Pure Python, stdlib only (unittest — matching this repo's existing pattern, e.g.
tests/test_vault_forensic_owner_thread.py). Runnable directly:

    python tests/test_soak_digest.py

Cases (named per the plan's Focused-tests list for this slice):
    - test_percentile_hand_computed             — percentile math against known values,
                                                    computed independently of any fixture file
    - test_golden_fixture_equivalence            — the committed golden digest is the contract
    - test_empty_input_honesty_missing_file       — digest says "no data", never fabricates
    - test_empty_input_honesty_empty_file         — same, for a present-but-empty events file
    - test_malformed_line_resilience              — a poisoned line is skipped and COUNTED,
                                                     never crashes the run
    - test_zero_failures_renders_plainly          — no failures -> plain statement, not an
                                                     empty table
    - test_warning_gate_folding_*                 — the W0 verdict text is parsed, not
                                                     re-implemented

NEGATIVE CONTROL (mandatory, manual — matches the pattern used by
tests/test_vault_forensic_owner_thread.py): this suite does not self-mutate the committed
golden fixture. To reproduce the control:

    1. Copy tests/fixtures/soak/events-golden.jsonl to a temp file.
    2. Change exactly one event's "durationMs" value (e.g. the first "open" event's 100 -> 999).
    3. Run this test with the env var override pointed at the mutated copy:
           set SOAK_DIGEST_GOLDEN_EVENTS_PATH=<path to mutated copy>          (cmd)
           $env:SOAK_DIGEST_GOLDEN_EVENTS_PATH = "<path>"                    (PowerShell)
           SOAK_DIGEST_GOLDEN_EVENTS_PATH=<path> python tests/test_soak_digest.py  (sh)
    4. Exactly test_golden_fixture_equivalence must fail, and its failure message must name
       the differing statistic(s) by dotted JSON path (e.g. "durationStatsByType.open.max:
       expected 500.0, got 999.0") — never just "files differ".
    5. Unset the env var (or don't set it) and rerun to confirm green again.
"""

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "soak-digest.py"
FIXTURES_DIR = REPO_ROOT / "tests" / "fixtures" / "soak"


def _load_soak_digest_module():
    """scripts/soak-digest.py has a hyphen in its filename, so it cannot be `import`ed
    normally — load it by file path instead."""
    spec = importlib.util.spec_from_file_location("soak_digest", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


sd = _load_soak_digest_module()


def _diff_digest(actual, expected, path: str = "") -> list:
    """Walk two JSON-shaped structures and report every differing leaf by dotted path —
    so a golden-equivalence failure always names the differing statistic, never just
    'files differ'."""
    diffs = []
    if isinstance(expected, dict) and isinstance(actual, dict):
        keys = sorted(set(expected.keys()) | set(actual.keys()))
        for k in keys:
            child_path = f"{path}.{k}" if path else k
            if k not in actual:
                diffs.append(f"{child_path}: missing from actual (expected {expected[k]!r})")
            elif k not in expected:
                diffs.append(f"{child_path}: unexpected in actual ({actual[k]!r})")
            else:
                diffs.extend(_diff_digest(actual[k], expected[k], child_path))
    elif isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            diffs.append(f"{path}: expected {len(expected)} item(s), got {len(actual)}")
        for i, (a_item, e_item) in enumerate(zip(actual, expected)):
            diffs.extend(_diff_digest(a_item, e_item, f"{path}[{i}]"))
    else:
        if actual != expected:
            diffs.append(f"{path}: expected {expected!r}, got {actual!r}")
    return diffs


class PercentileMathTest(unittest.TestCase):
    """Hand-computed, not self-referential: these expected values are arithmetic done by
    hand in the plan/report, not derived by calling the code under test."""

    def test_percentile_hand_computed_five_values(self):
        # sorted [100, 200, 300, 400, 500], n=5
        # p50: rank = ceil(0.50*5) = ceil(2.5) = 3 -> sorted[2] = 300
        # p95: rank = ceil(0.95*5) = ceil(4.75) = 5 -> sorted[4] = 500
        values = sorted([300, 100, 500, 200, 400])
        self.assertEqual(sd.nearest_rank_percentile(values, 50), 300)
        self.assertEqual(sd.nearest_rank_percentile(values, 95), 500)
        self.assertEqual(values[-1], 500)

    def test_percentile_hand_computed_four_values(self):
        # sorted [10, 20, 30, 40], n=4
        # p50: rank = ceil(0.50*4) = ceil(2.0) = 2 -> sorted[1] = 20
        # p95: rank = ceil(0.95*4) = ceil(3.8) = 4 -> sorted[3] = 40
        values = [10, 20, 30, 40]
        self.assertEqual(sd.nearest_rank_percentile(values, 50), 20)
        self.assertEqual(sd.nearest_rank_percentile(values, 95), 40)

    def test_percentile_single_value(self):
        self.assertEqual(sd.nearest_rank_percentile([42], 50), 42)
        self.assertEqual(sd.nearest_rank_percentile([42], 95), 42)

    def test_percentile_two_values(self):
        # sorted [400, 500], n=2
        # p50: rank = ceil(1.0) = 1 -> sorted[0] = 400
        # p95: rank = ceil(1.9) = 2 -> sorted[1] = 500
        values = [400, 500]
        self.assertEqual(sd.nearest_rank_percentile(values, 50), 400)
        self.assertEqual(sd.nearest_rank_percentile(values, 95), 500)


class GoldenFixtureEquivalenceTest(unittest.TestCase):
    """The committed golden digest is the contract. Env var override exists solely for the
    documented manual negative control (module docstring); do not set it for a normal run."""

    def _events_path(self) -> str:
        override = os.environ.get("SOAK_DIGEST_GOLDEN_EVENTS_PATH")
        if override:
            return override
        return str(FIXTURES_DIR / "events-golden.jsonl")

    def test_golden_fixture_equivalence(self):
        with tempfile.TemporaryDirectory() as tmp:
            digest = sd.run(
                session_root=str(FIXTURES_DIR),
                events_file=self._events_path(),
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            actual_md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")

        expected_md = (FIXTURES_DIR / "digest-golden.md").read_text(encoding="utf-8")

        # Recompute the expected structured digest independently (same inputs, same code
        # path as production use) so a JSON-level mismatch names the exact statistic.
        expected_result = sd.load_events(FIXTURES_DIR, str(FIXTURES_DIR / "events-golden.jsonl"))
        expected_digest = sd.build_digest(expected_result, {"available": False, "clean": None,
                                                              "failCount": 0, "failLines": []})

        diffs = _diff_digest(digest, expected_digest)
        # coverage.pathsTried / warningGate.path are filesystem-location diagnostics, not
        # computed statistics: they legitimately vary with invocation (env var override,
        # checkout location) and are never rendered into soak-digest.md. Excluding them here
        # does not weaken the contract — it keeps the negative control's signal from being
        # masked by an unrelated path-string difference.
        diffs = [
            d for d in diffs
            if not d.startswith("coverage.pathsTried") and not d.startswith("warningGate.path")
        ]
        self.assertEqual(
            diffs, [],
            "golden digest JSON mismatch (differing statistics):\n" + "\n".join(diffs),
        )

        self.assertEqual(
            actual_md, expected_md,
            "golden digest markdown mismatch — rendered output no longer matches "
            "tests/fixtures/soak/digest-golden.md",
        )


class EmptyInputHonestyTest(unittest.TestCase):
    def test_missing_events_file_says_no_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            digest = sd.run(
                session_root=tmp,
                events_file=None,
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")

        self.assertFalse(digest["coverage"]["hasData"])
        self.assertFalse(digest["coverage"]["eventsFileFound"])
        self.assertIn("No data", md)
        self.assertNotIn("Operation counts", md)
        # never a table pretending to be a clean pass
        self.assertNotIn("| Operation | Count |", md)

    def test_empty_events_file_says_no_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            events_path = Path(tmp) / "events.jsonl"
            events_path.write_text("", encoding="utf-8")
            digest = sd.run(
                session_root=tmp,
                events_file=str(events_path),
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")

        self.assertFalse(digest["coverage"]["hasData"])
        self.assertTrue(digest["coverage"]["eventsFileFound"])
        self.assertEqual(digest["coverage"]["totalLinesRead"], 0)
        self.assertIn("No data", md)
        self.assertIn("empty", md)


class MalformedLineResilienceTest(unittest.TestCase):
    def test_poisoned_line_is_skipped_and_counted_never_crashes(self):
        events_path = FIXTURES_DIR / "events-malformed.jsonl"
        result = sd.load_events(FIXTURES_DIR, str(events_path))

        # 4 non-blank lines total; 1 is unparseable JSON; 3 are valid events.
        self.assertEqual(result.total_lines, 4)
        self.assertEqual(result.malformed_lines, 1)
        self.assertEqual(len(result.events), 3)
        self.assertTrue(result.has_data)

        with tempfile.TemporaryDirectory() as tmp:
            digest = sd.run(
                session_root=str(FIXTURES_DIR),
                events_file=str(events_path),
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")

        self.assertEqual(digest["coverage"]["malformedLinesSkipped"], 1)
        self.assertEqual(digest["coverage"]["totalEventsValid"], 3)
        self.assertIn("1 line(s) could not be read and were skipped", md)


class ZeroFailuresRendersPlainlyTest(unittest.TestCase):
    def test_zero_failures_is_plain_statement_not_empty_table(self):
        events_path = FIXTURES_DIR / "events-zero-failures.jsonl"
        with tempfile.TemporaryDirectory() as tmp:
            digest = sd.run(
                session_root=str(FIXTURES_DIR),
                events_file=str(events_path),
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")

        self.assertEqual(digest["failures"]["totalFailEvents"], 0)
        self.assertEqual(digest["failures"]["bySubject"], [])
        self.assertIn("No failures recorded", md)
        # the empty-table anti-pattern: a header row with zero data rows under it
        self.assertNotIn("| Subject | Times it failed | First time | Last time |", md)


class WarningGateFoldingTest(unittest.TestCase):
    """This script only PARSES the gate's own printed verdict — it must never re-derive a
    clean/failed verdict from raw log text itself (that is warning_gate.ps1's job)."""

    def test_not_available_when_no_warnings_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            digest = sd.run(
                session_root=tmp,
                events_file=str(FIXTURES_DIR / "events-zero-failures.jsonl"),
                warnings_file=None,
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")
        self.assertFalse(digest["warningGate"]["available"])
        self.assertIn("Not available for this run", md)

    def test_clean_verdict_is_folded_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            warnings_path = Path(tmp) / "warnings.txt"
            warnings_path.write_text("WARNING_GATE_OK\n", encoding="utf-8")
            digest = sd.run(
                session_root=tmp,
                events_file=str(FIXTURES_DIR / "events-zero-failures.jsonl"),
                warnings_file=str(warnings_path),
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")
        self.assertTrue(digest["warningGate"]["clean"])
        self.assertIn("Clean", md)

    def test_failed_verdict_names_the_offending_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            warnings_path = Path(tmp) / "warnings.txt"
            warnings_path.write_text(
                "FAIL: [colosseum.log] 2026-08-12 00:00:00.000 [W] something odd\n"
                "FAIL: [stderr.log] Cannot load nvcuda.dll\n",
                encoding="utf-8",
            )
            digest = sd.run(
                session_root=tmp,
                events_file=str(FIXTURES_DIR / "events-zero-failures.jsonl"),
                warnings_file=str(warnings_path),
                out_dir=tmp,
                label=None,
            )
            md = (Path(tmp) / "soak-digest.md").read_text(encoding="utf-8")
        self.assertFalse(digest["warningGate"]["clean"])
        self.assertEqual(digest["warningGate"]["failCount"], 2)
        self.assertIn("Cannot load nvcuda.dll", md)


if __name__ == "__main__":
    print(f"[test_soak_digest] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)

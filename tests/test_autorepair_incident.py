#!/usr/bin/env python3
"""test_autorepair_incident.py - tests for scripts/autorepair/incident.py (Guardian Loop G3).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G3 ("The incident
packet - from a failed run directory to an agent-grade bug report"). Pure Python, stdlib
unittest - house flat convention (D8: tests/test_autorepair_*.py, not a subdir), matching
tests/test_autorepair_policy.py and tests/test_autorepair_sandbox.py. Runnable directly:

    python tests/test_autorepair_incident.py -v

The golden fixtures this file exercises (both committed, both read-only inputs - never
mutated in place; every test that needs to corrupt one works on a tempfile.TemporaryDirectory
copy):

  tests/fixtures/autorepair/golden-run-dir/    a real failed `lanista session run` captured
      this slice: journey_open_manga.json driven against a scratch copy of
      tests/lanista_fixtures/journeys/open-manga-v1 whose journey-manga-ch-1 page images
      were corrupted (see incident.json's own seed field and this file's module docstring
      note below for exactly what happened and why the failing step is NOT readerReady).

  tests/fixtures/autorepair/golden-incident/   incident.py's own output packet for the run
      dir above, minted with a FIXED timestamp (2026-08-14T16:30:00Z) into an empty,
      dedicated artifacts root so its `id`/`createdAt`/`fingerprint` are fully
      deterministic - re-running build_incident() the same way must reproduce it exactly,
      field for field, except `baseSha`/environment.json (live git HEAD - see
      GoldenFixtureEquivalenceTests) and `runDir` (points at the caller's own temp copy).

IMPORTANT ground-truth note (loud, per the executing brief): the plan's own text assumed
corrupting the seed's page images would make the `ui-wait-for comicReaderShell.readerReady`
step TIME OUT. Live-tested this slice: it does NOT. ComicReaderDoubleSurface.qml deliberately
treats a terminal decode error as "resolved" (a page shows an error placard) exactly like a
successfully decoded page - readerReady genuinely goes true, on schedule, with all three
pages broken (see ComicReaderDoubleSurface.qml's "WHAT THE SCREEN HAS" comment - error
placards are load-bearing there, not an oversight). The corrupted seed instead breaks a
LATER step: after closing and reopening the reader, "the seeded chapter's shelf card
renders again" times out (WAIT_TIMEOUT, 15s) - a real, reproducible downstream effect of the
same corruption, just not the one the plan predicted. The golden fixture captures this
actual, live-observed failure, not a fabricated one - see the executing agent's final report
for the full session transcript proving the divergence.

Six groups of cases, matching the G3 slice's own "Focused tests" contract:

  GoldenFixtureEquivalenceTests   golden-fixture equivalence: build_incident() on a temp
                                   copy of golden-run-dir, pinned to the same FIXED_NOW,
                                   reproduces golden-incident/incident.json field for field
                                   (baseSha/runDir excluded - see class docstring); D8 file
                                   set present; ui-tree.json's honest absence; vault-
                                   forensics.json correctly absent; warnings.json reports
                                   the corruption's real Qt warnings (not a vacuous pass).

  MalformedRunDirTests             run dir not found; missing/invalid session.json; missing
                                    failure.log; empty failure.log; unparsable invocation
                                    header; internally-inconsistent summary line. Each is a
                                    named MalformedRunDirError, never a bare traceback.

  NegativeControlSessionJsonTests  THE slice's mandated negative control, both directions:
                                    delete session.json from a TEMP copy of golden-run-dir
                                    -> build_incident() refuses with MalformedRunDirError
                                    naming session.json; restore it -> green again, and the
                                    rebuilt packet matches the golden shape.

  NoFailureRegressionTests         a run dir describing an all-PASS run -> NoFailureError
                                    ("no failure to report"), by design (D8's own regression
                                    path).

  FingerprintAndDedupTests         compute_fingerprint() sanity (same input -> same hash,
                                    any field changing -> a different hash); A7: a second
                                    incident whose scenario/failing-label/expected/got match
                                    an already-open incident is refused
                                    (DuplicateIncidentError) and points at the first; a
                                    DIFFERENT failing step is NOT treated as a duplicate; a
                                    `CLOSED` marker on the first incident's dir exempts it
                                    from the scan (forward-compatible escape hatch, unused by
                                    any slice today).

  ParseFailureLogTests             parse_failure_log() unit-level: header parsing (quoted
                                    Windows paths, --seed/--tag/--exe/--qml/--ready-ms,
                                    --drive/--verbose), PASS/FAIL/INFRA line parsing
                                    (INFRA's one-space prefix vs PASS/FAIL's two), detail/
                                    evidence splitting, empty-log refusal.

All temp-copy fixtures operate on a tempfile.TemporaryDirectory() seeded from
shutil.copytree(GOLDEN_RUN_DIR, ...) or hand-built minimal run dirs (_write_run_dir below) -
the committed tests/fixtures/autorepair/* originals are read-only inputs to every test in
this file and are never mutated in place.
"""
from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "autorepair" / "incident.py"
GOLDEN_RUN_DIR = REPO_ROOT / "tests" / "fixtures" / "autorepair" / "golden-run-dir"
GOLDEN_INCIDENT_DIR = REPO_ROOT / "tests" / "fixtures" / "autorepair" / "golden-incident"

# The exact timestamp golden-incident/ was minted with (see this file's module docstring) -
# reusing it makes a fresh build_incident() call reproduce `id` and `createdAt` exactly.
FIXED_NOW = datetime(2026, 8, 14, 16, 30, 0, tzinfo=timezone.utc)

# incident.json fields that are inherently NOT reproducible from the frozen run-dir input
# alone: baseSha reflects the LIVE git HEAD at build time (moves with every commit),
# runDir echoes back whatever path the caller passed in (a fresh temp dir per test).
VOLATILE_INCIDENT_FIELDS = {"baseSha", "runDir"}


def _load_incident_module():
    spec = importlib.util.spec_from_file_location("autorepair_incident", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


mod = _load_incident_module()


def _copy_golden_run_dir(dest_parent: Path, name: str = "run") -> Path:
    dest = dest_parent / name
    shutil.copytree(GOLDEN_RUN_DIR, dest)
    return dest


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


MINIMAL_SESSION = {
    "schema": "colosseum.session.v1",
    "sessionId": "20260101-000000-deadbeef",
    "tag": "synthtag",
    "pipe": "ColosseumLanista-20260101-000000-deadbeef",
    "exe": "C:/fake/native/build-msvc/colosseum.exe",
    "exeSha256": "0" * 64,
    "qml": "qml/Main.qml",
    "pid": 1,
    "drive": True,
    "seedDir": "C:/fake/seed",
    "launchedAt": "2026-01-01T00:00:00.000",
    "readyAt": "2026-01-01T00:00:05.000",
    "appDataRoot": "C:/fake/appdata",
    "cacheRoot": "C:/fake/cache",
    "stdoutPath": "artifacts/lanista-sessions/fake/stdout.log",
    "stderrPath": "artifacts/lanista-sessions/fake/stderr.log",
    "exitedAt": "2026-01-01T00:00:10.000",
    "exitCode": 0,
    "crashed": False,
    "killReason": "graceful",
}


def _write_run_dir(
    dest: Path,
    *,
    steps: list[tuple[str, str, str]],
    exit_code: int = 1,
    scenario: str = "tests/lanista_scenarios/journey_open_manga.json",
    tag: str = "synthtag",
    failed_override: int | None = None,
    total_override: int | None = None,
    omit_invocation_header: bool = False,
) -> Path:
    """Build a MINIMAL synthetic run dir: session.json + failure.log only (stdout.log/
    stderr.log/colosseum.log/*.png are all optional per the run-dir contract - see
    scripts/autorepair/incident.py's module docstring). `steps` is a list of
    (status, label, detail) triples in lanista.cpp's own console order."""
    dest.mkdir(parents=True, exist_ok=True)
    session = dict(MINIMAL_SESSION)
    session["tag"] = tag
    (dest / "session.json").write_text(json.dumps(session, indent=2), encoding="utf-8")

    lines: list[str] = []
    if not omit_invocation_header:
        lines.append(
            f"$ native/build-msvc/lanista.exe session run {scenario} --seed C:/fake/seed "
            f"--tag {tag} --drive --ready-ms 90000 --verbose"
        )
    lines.append("SESSION 20260101-000000-deadbeef pipe=ColosseumLanista-20260101-000000-deadbeef")
    for status, label, detail in steps:
        line = f"{status}  {label}"
        if detail:
            line += f"  [{detail}]"
        lines.append(line)
    failed = failed_override if failed_override is not None else sum(
        1 for s in steps if s[0] != "PASS"
    )
    total = total_override if total_override is not None else len(steps)
    lines.append(f"{total} steps, {failed} failed  (manifest: {dest}/session.json)")
    lines.append(f"EXIT_CODE: {exit_code}")
    (dest / "failure.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return dest


ALL_PASS_STEPS = [
    ("PASS", "the journey session answers, drive gated on", ""),
    ("PASS", "one visible window, isolated data root", ""),
    ("PASS", "the boot splash has left the screen", ""),
]

ONE_FAIL_STEPS = [
    ("PASS", "the journey session answers, drive gated on", ""),
    ("PASS", "one visible window, isolated data root", ""),
    ("FAIL", "readerReady is the real false->true transition, no sleep",
     "matched == true — got undefined"),
]


# ══════════════════════════════════════════════════════════════════════════
# Part A - golden-fixture equivalence
# ══════════════════════════════════════════════════════════════════════════

class GoldenFixtureEquivalenceTests(unittest.TestCase):
    def _build_from_golden(self, tmp: Path) -> "mod.IncidentResult":
        run_dir = _copy_golden_run_dir(tmp)
        artifacts_root = tmp / "artifacts-autorepair"
        return mod.build_incident(run_dir, artifacts_root=artifacts_root, now=FIXED_NOW)

    def test_incident_json_matches_golden_field_for_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            fresh = dict(result.incident)
            golden = _load_json(GOLDEN_INCIDENT_DIR / "incident.json")

            for key in VOLATILE_INCIDENT_FIELDS:
                fresh.pop(key, None)
                golden.pop(key, None)
            self.assertEqual(fresh, golden)

    def test_baseSha_is_a_real_git_sha_not_the_golden_literal(self):
        """baseSha is deliberately excluded from the equality check above because it
        reflects the LIVE git HEAD, which moves with every commit landed after this slice -
        assert it independently: present, and shaped like a git sha."""
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            base_sha = result.incident["baseSha"]
            self.assertIsInstance(base_sha, str)
            self.assertEqual(len(base_sha), 40)
            int(base_sha, 16)  # raises ValueError if not hex

    def test_d8_file_set_present(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            names = {p.name for p in result.dir.iterdir()}
            expected = {
                "incident.json", "failure.log", "stdout.log", "stderr.log",
                "colosseum.log", "journey.json", "grabs", "screen.png",
                "warnings.json", "ui-tree.json", "environment.json", "reproduce.ps1",
            }
            self.assertTrue(expected.issubset(names), names)
            # This journey does not touch Vault surfaces - correctly absent (D8/G3).
            self.assertNotIn("vault-forensics.json", names)

    def test_journey_json_matches_the_live_scenario_tree(self):
        """journey.json is read from the CURRENT tree (D1), not frozen at run time - so it
        is compared against a fresh read of the same file, not against a frozen golden
        copy (which would drift the moment the scenario file itself is edited)."""
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            packet_journey = _load_json(result.dir / "journey.json")
            live_scenario = _load_json(
                REPO_ROOT / "tests" / "lanista_scenarios" / "journey_open_manga.json"
            )
            self.assertEqual(packet_journey, live_scenario)

    def test_ui_tree_json_is_an_honest_absence_never_fabricated(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            ui_tree = _load_json(result.dir / "ui-tree.json")
            self.assertFalse(ui_tree["available"])
            self.assertIn("post-mortem", ui_tree["reason"])

    def test_warnings_json_reports_the_corruption_real_qt_warnings(self):
        """Not a vacuous pass: the corrupted fixture genuinely produced Qt image-decode
        warnings (ComicReaderDecode + QQuickImage), so the warning gate must FAIL, not
        silently report WARNING_GATE_OK."""
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            warnings = _load_json(result.dir / "warnings.json")
            if not warnings.get("invoked"):
                self.skipTest(f"warning gate not invokable in this environment: {warnings}")
            self.assertEqual(warnings["verdict"], "FAIL")
            self.assertGreater(len(warnings["output"]), 0)
            self.assertTrue(any("Unsupported image format" in ln for ln in warnings["output"]))

    def test_failing_step_is_the_reopen_shelf_card_timeout_not_readerReady(self):
        """Ground-truth pin for this slice (see module docstring): the plan assumed
        corrupting the page images would time out readerReady. It does not - readerReady
        genuinely settles true even with every page broken. Pin the ACTUAL observed
        failure so a future edit to incident.py can't silently drift back toward the
        (wrong) assumption without this test catching it."""
        with tempfile.TemporaryDirectory() as tmp:
            result = self._build_from_golden(Path(tmp))
            step = result.incident["failingStep"]
            self.assertIn("reopen", step["label"])
            self.assertIn("shelf card", step["label"])
            self.assertNotIn("readerReady", step["label"])


# ══════════════════════════════════════════════════════════════════════════
# Part B - malformed run dir -> clean named errors, never a half-written packet
# ══════════════════════════════════════════════════════════════════════════

class MalformedRunDirTests(unittest.TestCase):
    def test_run_dir_not_found(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(Path(tmp) / "does-not-exist",
                                    artifacts_root=Path(tmp) / "out")
            self.assertIn("not found", str(ctx.exception))

    def test_session_json_invalid_json_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(Path(tmp) / "run", steps=ONE_FAIL_STEPS)
            (run_dir / "session.json").write_text("{not valid json", encoding="utf-8")
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("session.json", str(ctx.exception))

    def test_missing_failure_log_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(Path(tmp) / "run", steps=ONE_FAIL_STEPS)
            (run_dir / "failure.log").unlink()
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("failure.log", str(ctx.exception))

    def test_empty_failure_log_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(Path(tmp) / "run", steps=ONE_FAIL_STEPS)
            (run_dir / "failure.log").write_text("", encoding="utf-8")
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("empty", str(ctx.exception))

    def test_missing_invocation_header_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(
                Path(tmp) / "run", steps=ONE_FAIL_STEPS, omit_invocation_header=True,
            )
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("invocation header", str(ctx.exception))

    def test_inconsistent_summary_line_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(
                Path(tmp) / "run", steps=ONE_FAIL_STEPS, failed_override=99,
            )
            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("inconsistent", str(ctx.exception))


# ══════════════════════════════════════════════════════════════════════════
# Part C - the mandated negative control (both directions)
# ══════════════════════════════════════════════════════════════════════════

class NegativeControlSessionJsonTests(unittest.TestCase):
    def test_delete_session_json_refuses_then_restore_is_green(self):
        """The plan's mandatory G3 negative control: delete session.json from a TEMP copy
        of the run dir -> builder refuses with a named error; restore -> green."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            run_dir = _copy_golden_run_dir(tmp_path)
            artifacts_root = tmp_path / "artifacts-autorepair"

            session_json = run_dir / "session.json"
            saved_bytes = session_json.read_bytes()
            session_json.unlink()

            with self.assertRaises(mod.MalformedRunDirError) as ctx:
                mod.build_incident(run_dir, artifacts_root=artifacts_root, now=FIXED_NOW)
            self.assertIn("session.json", str(ctx.exception))
            self.assertIn("missing", str(ctx.exception))

            # Restore (both directions preserved) and confirm green again.
            session_json.write_bytes(saved_bytes)
            result = mod.build_incident(run_dir, artifacts_root=artifacts_root, now=FIXED_NOW)
            self.assertEqual(result.id, "AR-2026-08-14-0001")
            self.assertEqual(result.incident["sessionId"], "20260814-162630-8be393f8")


# ══════════════════════════════════════════════════════════════════════════
# Part D - a PASSING run dir is a regression path, not a crash
# ══════════════════════════════════════════════════════════════════════════

class NoFailureRegressionTests(unittest.TestCase):
    def test_all_passing_run_dir_refused_no_failure_to_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(
                Path(tmp) / "run", steps=ALL_PASS_STEPS, exit_code=0,
            )
            with self.assertRaises(mod.NoFailureError) as ctx:
                mod.build_incident(run_dir, artifacts_root=Path(tmp) / "out")
            self.assertIn("no failure to report", str(ctx.exception))

    def test_passing_run_dir_creates_no_incident_directory(self):
        """A refusal must never leave a half-written packet behind."""
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = _write_run_dir(
                Path(tmp) / "run", steps=ALL_PASS_STEPS, exit_code=0,
            )
            out = Path(tmp) / "out"
            with self.assertRaises(mod.NoFailureError):
                mod.build_incident(run_dir, artifacts_root=out)
            self.assertFalse(out.exists())


# ══════════════════════════════════════════════════════════════════════════
# Part E - A7 fingerprint + dedup
# ══════════════════════════════════════════════════════════════════════════

class FingerprintAndDedupTests(unittest.TestCase):
    def test_same_inputs_same_fingerprint(self):
        a = mod.compute_fingerprint("s.json", "label", "x == 1", "0")
        b = mod.compute_fingerprint("s.json", "label", "x == 1", "0")
        self.assertEqual(a, b)

    def test_any_differing_field_changes_the_fingerprint(self):
        base = mod.compute_fingerprint("s.json", "label", "x == 1", "0")
        self.assertNotEqual(base, mod.compute_fingerprint("other.json", "label", "x == 1", "0"))
        self.assertNotEqual(base, mod.compute_fingerprint("s.json", "other label", "x == 1", "0"))
        self.assertNotEqual(base, mod.compute_fingerprint("s.json", "label", "x == 2", "0"))
        self.assertNotEqual(base, mod.compute_fingerprint("s.json", "label", "x == 1", "1"))

    def test_second_incident_same_fingerprint_refused_pointing_to_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifacts_root = Path(tmp) / "out"
            run_a = _write_run_dir(Path(tmp) / "run-a", steps=ONE_FAIL_STEPS, tag="taga")
            first = mod.build_incident(run_a, artifacts_root=artifacts_root, now=FIXED_NOW)

            # A different run dir (different tag/session), but the SAME scenario + failing
            # label + expected/got - a re-run of the identical flake.
            run_b = _write_run_dir(Path(tmp) / "run-b", steps=ONE_FAIL_STEPS, tag="tagb")
            with self.assertRaises(mod.DuplicateIncidentError) as ctx:
                mod.build_incident(run_b, artifacts_root=artifacts_root, now=FIXED_NOW)
            self.assertEqual(ctx.exception.existing_dir, first.dir)
            # No second incident directory was created for the duplicate.
            self.assertEqual(
                sorted(p.name for p in artifacts_root.iterdir()), [first.id],
            )

    def test_different_failing_step_is_not_a_duplicate(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifacts_root = Path(tmp) / "out"
            run_a = _write_run_dir(Path(tmp) / "run-a", steps=ONE_FAIL_STEPS, tag="taga")
            first = mod.build_incident(run_a, artifacts_root=artifacts_root, now=FIXED_NOW)

            different_steps = [
                ("PASS", "the journey session answers, drive gated on", ""),
                ("FAIL", "a totally different step", "matched == true — got false"),
            ]
            run_c = _write_run_dir(Path(tmp) / "run-c", steps=different_steps, tag="tagc")
            second = mod.build_incident(run_c, artifacts_root=artifacts_root, now=FIXED_NOW)
            self.assertNotEqual(first.id, second.id)
            self.assertNotEqual(first.incident["fingerprint"], second.incident["fingerprint"])

    def test_closed_marker_exempts_an_incident_from_dedup(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifacts_root = Path(tmp) / "out"
            run_a = _write_run_dir(Path(tmp) / "run-a", steps=ONE_FAIL_STEPS, tag="taga")
            first = mod.build_incident(run_a, artifacts_root=artifacts_root, now=FIXED_NOW)

            (first.dir / "CLOSED").write_text("closed for this test\n", encoding="utf-8")

            run_b = _write_run_dir(Path(tmp) / "run-b", steps=ONE_FAIL_STEPS, tag="tagb")
            second = mod.build_incident(run_b, artifacts_root=artifacts_root, now=FIXED_NOW)
            self.assertNotEqual(first.id, second.id)


# ══════════════════════════════════════════════════════════════════════════
# Part F - parse_failure_log() unit-level
# ══════════════════════════════════════════════════════════════════════════

class ParseFailureLogTests(unittest.TestCase):
    def test_header_parses_flags_and_quoted_paths(self):
        text = (
            '$ native/build-msvc/lanista.exe session run '
            'tests/lanista_scenarios/journey_open_manga.json '
            '--seed "C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/artifacts/x y" '
            '--tag g3red --drive --ready-ms 90000 --verbose\n'
            'PASS  a step\n'
            '1 steps, 0 failed  (manifest: x/session.json)\n'
            'EXIT_CODE: 0\n'
        )
        parsed = mod.parse_failure_log(text)
        inv = parsed.invocation
        self.assertEqual(inv["lanistaExe"], "native/build-msvc/lanista.exe")
        self.assertEqual(inv["scenario"], "tests/lanista_scenarios/journey_open_manga.json")
        self.assertEqual(inv["seed"], "C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/artifacts/x y")
        self.assertEqual(inv["tag"], "g3red")
        self.assertTrue(inv["drive"])
        self.assertTrue(inv["verbose"])
        self.assertEqual(inv["readyMs"], 90000)
        self.assertEqual(parsed.exitCode, 0)

    def test_infra_line_single_space_prefix_parses(self):
        """lanista.cpp prints "INFRA " (one trailing space) vs "PASS  "/"FAIL  " (two) -
        both must parse."""
        text = (
            "$ lanista.exe session run s.json --tag t\n"
            "INFRA the bridge died: NO_PIPE: pipe closed\n"
            "1 steps, 1 failed  (manifest: x/session.json)\n"
        )
        parsed = mod.parse_failure_log(text)
        self.assertEqual(len(parsed.steps), 1)
        self.assertEqual(parsed.steps[0].status, "INFRA")
        self.assertEqual(parsed.steps[0].label, "the bridge died: NO_PIPE: pipe closed")
        self.assertEqual(parsed.failedCount, 1)

    def test_detail_and_evidence_split_correctly(self):
        text = (
            "$ lanista.exe session run s.json --tag t\n"
            "FAIL  readerReady settles  [matched == true — got undefined]  "
            "evidence: C:/runs/x/seq1.png\n"
            "1 steps, 1 failed  (manifest: x/session.json)\n"
        )
        parsed = mod.parse_failure_log(text)
        step = parsed.steps[0]
        self.assertEqual(step.label, "readerReady settles")
        self.assertEqual(step.detail, "matched == true — got undefined")
        self.assertEqual(step.evidence, "C:/runs/x/seq1.png")

    def test_step_with_no_detail_and_no_evidence(self):
        text = (
            "$ lanista.exe session run s.json --tag t\n"
            "PASS  open the taskbar dock\n"
            "1 steps, 0 failed  (manifest: x/session.json)\n"
        )
        parsed = mod.parse_failure_log(text)
        step = parsed.steps[0]
        self.assertEqual(step.label, "open the taskbar dock")
        self.assertEqual(step.detail, "")
        self.assertEqual(step.evidence, "")

    def test_empty_text_refused(self):
        with self.assertRaises(mod.MalformedRunDirError):
            mod.parse_failure_log("")

    def test_no_step_lines_refused(self):
        with self.assertRaises(mod.MalformedRunDirError):
            mod.parse_failure_log("$ lanista.exe session run s.json --tag t\nnothing here\n")


if __name__ == "__main__":
    print(f"[test_autorepair_incident] script under test: {SCRIPT_PATH}")
    unittest.main(verbosity=2)

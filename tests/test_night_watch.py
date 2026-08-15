#!/usr/bin/env python3
"""test_night_watch.py - tests for scripts/night_watch.py (the N0 watchman + G10
trigger slice).

docs/superpowers/plans/2026-08-15-colosseum-night-watch-n0-plan.md, and the G10
acceptance tests the guardian-loop plan itself named: `failed_run_opens_incident`
and `flag_off_changes_nothing`.

HERMETIC: a FAKE lanista (a generated .cmd wrapper around a small Python stub)
creates real run dirs under a temp lanista-sessions root and prints a real-shaped
lanista step trace (PASS/FAIL lines with the literal em-dash detail format, the
"N steps, M failed" summary - formats pinned against the D10 rehearsal's own
captured failure.log). The REAL incident builder then mints from those run dirs
against a temp artifacts root - the minting path is exercised for real, not
mocked. The Guardian launch is spied (never built - no sandbox, no model call).
The module's path constants are repointed at a temp "repo" in setUp.

    python tests/test_night_watch.py -v
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
SCRIPT_PATH = REPO_ROOT / "scripts" / "night_watch.py"


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


nw = _load_module("night_watch_under_test", SCRIPT_PATH)

_FAKE_LANISTA_PY = r"""
import json, os, sys, uuid
from datetime import datetime, timezone
from pathlib import Path

argv = sys.argv[1:]
scenario = next(a for a in argv if a.endswith(".json"))
sessions = Path(os.environ["NW_FAKE_SESSIONS"])
stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
run_dir = sessions / f"{stamp}-{uuid.uuid4().hex[:8]}"
run_dir.mkdir(parents=True)
now = datetime.now(timezone.utc).isoformat()
(run_dir / "session.json").write_text(json.dumps({
    "pid": os.getpid(), "drive": True, "launchedAt": now, "readyAt": now,
    "exitedAt": now, "exitCode": 0, "crashed": False, "killReason": None,
}), encoding="utf-8")
(run_dir / "stdout.log").write_text("app stdout\n", encoding="utf-8")
print(f"SESSION {run_dir.name}")
if "red" in Path(scenario).name:
    print("PASS  the app came up")
    print("FAIL  the readiness wait  [shell.readerReady == true \u2014 got undefined]  "
          f"evidence: {run_dir}/screen.png")
    print(f"2 steps, 1 failed  (manifest: {run_dir}/session.json)")
    sys.exit(1)
print("PASS  the app came up")
print("PASS  everything held")
print(f"2 steps, 0 failed  (manifest: {run_dir}/session.json)")
sys.exit(0)
"""


class _FakeGatePolicy:
    def __init__(self, gate: bool):
        self.policy = {"nightWatchAutoRepair": gate}


class _NightWatchHarness(unittest.TestCase):
    """Shared hermetic harness: temp repo, fake lanista, repointed path constants,
    spied Guardian, and a PINNED model backend. The pin matters: the resume test
    legitimately passes --brain handoff, and live_runners' backend is a sticky
    module global - unpinned, every later test would resolve handoff and the
    mind legs would fire REAL file-handoff brain calls (write into the true
    artifacts/glm-brain and wait out the timeout). Pinned to claude, the mind
    legs' handoff gate keeps every unspied test bundle-only."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="n0test_")
        tmp = Path(self._tmp.name)

        self.repo = tmp / "repo"
        (self.repo / "tests" / "lanista_scenarios").mkdir(parents=True)
        for name in ("red_one.json", "green_one.json"):
            (self.repo / "tests" / "lanista_scenarios" / name).write_text(
                json.dumps({"name": name, "steps": []}), encoding="utf-8"
            )

        self.sessions = tmp / "lanista-sessions"
        self.sessions.mkdir()
        self.artifacts = tmp / "autorepair"
        self.reports = tmp / "night-watch"

        stub = tmp / "fake_lanista.py"
        stub.write_text(_FAKE_LANISTA_PY, encoding="utf-8")
        wrapper = tmp / "fake_lanista.cmd"
        wrapper.write_text(f'@"{sys.executable}" "{stub}" %*\n', encoding="utf-8")
        app = tmp / "colosseum.exe"
        app.write_bytes(b"")

        self._saved = {
            name: getattr(nw, name) for name in
            ("REPO_ROOT", "LANISTA_EXE", "APP_EXE", "LANISTA_SESSIONS_DIR",
             "DEFAULT_ARTIFACTS_ROOT", "NIGHT_WATCH_ROOT", "load_policy")
        }
        nw.REPO_ROOT = self.repo
        nw.LANISTA_EXE = wrapper
        nw.APP_EXE = app
        nw.LANISTA_SESSIONS_DIR = self.sessions
        nw.DEFAULT_ARTIFACTS_ROOT = self.artifacts
        nw.NIGHT_WATCH_ROOT = self.reports
        nw.load_policy = lambda: _FakeGatePolicy(gate=False)
        self._env_token = "NW_FAKE_SESSIONS"
        self._saved_env = os.environ.get(self._env_token)
        os.environ[self._env_token] = str(self.sessions)

        self._guardian_calls: list[Path] = []
        self._guardian_kwargs: list[dict] = []
        self._saved_run_guardian = nw.run_guardian
        nw.run_guardian = self._guardian_spy

        self._saved_backend = nw.live_runners._model_backend
        nw.live_runners._model_backend = "claude"

        # The disk guard reads the REAL machine disk through the repointed
        # REPO_ROOT (temp dirs live on C:) - a nearly-full C: would flip tests
        # into SKIPPED-LOW-DISK. Pin it high: these tests own the gate and the
        # mind, never the machine's disk state.
        self._saved_free_disk = nw._free_disk_bytes
        nw._free_disk_bytes = lambda: 8 * 1024**3

    def tearDown(self):
        nw._free_disk_bytes = self._saved_free_disk
        nw.live_runners._model_backend = self._saved_backend
        nw.run_guardian = self._saved_run_guardian
        for name, value in self._saved.items():
            setattr(nw, name, value)
        if self._saved_env is None:
            os.environ.pop(self._env_token, None)
        else:
            os.environ[self._env_token] = self._saved_env
        self._tmp.cleanup()

    def _guardian_spy(self, incident_dir: Path, **kwargs):
        self._guardian_calls.append(incident_dir)
        self._guardian_kwargs.append(kwargs)
        return {"terminalState": "DOCUMENTED", "ranStages": ["triage", "diagnosis"]}

    def _run_main(self, *extra: str) -> int:
        return nw.main(["--once", "--pause-sec", "0",
                        "--scenarios", "tests/lanista_scenarios/*.json", *extra])

    def _report_dir(self) -> Path:
        reports = sorted(self.reports.iterdir())
        self.assertTrue(reports, "no night-watch report dir was written")
        return reports[-1]

    def _report_text(self) -> str:
        return (self._report_dir() / "report.md").read_text(encoding="utf-8")


class NightWatchTests(_NightWatchHarness):

    # ---- G10 acceptance: failed_run_opens_incident ----

    def test_red_run_opens_incident_with_failure_log_contract(self):
        self.assertEqual(self._run_main(), 0)
        incident_dirs = [d for d in self.artifacts.iterdir() if d.is_dir()]
        self.assertEqual(len(incident_dirs), 1, "exactly one incident from one red run")
        incident_dir = incident_dirs[0]
        self.assertTrue((incident_dir / "incident.json").is_file())
        failure = (incident_dir / "failure.log").read_text(encoding="utf-8")
        self.assertTrue(failure.startswith("$ "), "failure.log must open with the $ <cmd> header")
        self.assertIn("FAIL  the readiness wait", failure)
        incident = json.loads((incident_dir / "incident.json").read_text(encoding="utf-8"))
        self.assertEqual(incident["failingStep"]["label"], "the readiness wait")
        self.assertEqual(incident["failedCount"], 1)
        self.assertIn("red_one.json", incident["scenario"])

    # ---- G10 acceptance: flag_off_changes_nothing ----

    def test_flag_off_launches_no_guardian(self):
        nw.load_policy = lambda: _FakeGatePolicy(gate=False)
        self.assertEqual(self._run_main(), 0)
        self.assertEqual(self._guardian_calls, [], "gate off: the Guardian must never launch")
        report = self._report_text()
        self.assertIn("1 red", report)
        self.assertIn("Guardian gate (policy.nightWatchAutoRepair): off", report)
        # the incident itself is STILL opened and listed (G10 step 1 is ungated);
        # only the Guardian launch (step 2) is gated - and the report says so.
        self.assertIn("## Incidents opened", report)
        self.assertIn("AR-", report)
        self.assertIn("GATE-OFF", report)

    def test_flag_on_launches_guardian_on_each_opened_incident(self):
        nw.load_policy = lambda: _FakeGatePolicy(gate=True)
        self.assertEqual(self._run_main(), 0)
        self.assertEqual(len(self._guardian_calls), 1)
        minted = [d for d in self.artifacts.iterdir() if d.is_dir()][0]
        self.assertEqual(self._guardian_calls[0], minted)
        self.assertIn("DOCUMENTED", self._report_text())

    # ---- dedup: one flake never becomes five incidents ----

    def test_second_red_of_same_fingerprint_is_duplicate_not_fatal(self):
        nw.load_policy = lambda: _FakeGatePolicy(gate=True)
        self.assertEqual(
            nw.main(["--cycles", "2", "--pause-sec", "0",
                     "--scenarios", "tests/lanista_scenarios/red_one.json"]),
            0,
        )
        incident_dirs = [d for d in self.artifacts.iterdir() if d.is_dir()]
        self.assertEqual(len(incident_dirs), 1, "dedup: cycle 2's identical red opens nothing")
        report = self._report_text()
        self.assertIn("duplicate", report)
        # the Guardian ran for the FIRST red only
        self.assertEqual(len(self._guardian_calls), 1)

    def test_green_never_mints(self):
        self.assertEqual(
            nw.main(["--once", "--pause-sec", "0",
                     "--scenarios", "tests/lanista_scenarios/green_one.json"]),
            0,
        )
        self.assertFalse(self.artifacts.exists() and any(self.artifacts.iterdir()))
        self.assertIn("0 red", self._report_text())

    # ---- operator footguns ----

    def test_typoed_scenarios_glob_is_hard_error(self):
        with self.assertRaises(nw.NightWatchError):
            nw.resolve_scenarios(["tests/lanista_scenarios/nope_*.json"])

    def test_dry_run_runs_nothing(self):
        rc = nw.main(["--dry-run", "--scenarios", "tests/lanista_scenarios/green_one.json"])
        self.assertEqual(rc, 0)
        self.assertFalse(any(self.sessions.iterdir()))
        self.assertFalse(self.reports.exists() and any(self.reports.iterdir()))

    # ---- --resume: hand the Guardian one existing incident dir, run no scenarios ----

    def test_resume_runs_guardian_on_dir_with_jobs_and_no_scenarios(self):
        incident_dir = self.artifacts / "AR-test-0001"
        incident_dir.mkdir(parents=True)
        (incident_dir / "incident.json").write_text('{"id": "AR-test-0001"}', encoding="utf-8")

        rc = nw.main(["--resume", str(incident_dir), "--jobs", "4", "--brain", "handoff"])

        self.assertEqual(rc, 0)
        self.assertEqual(self._guardian_calls, [incident_dir])
        self.assertEqual(self._guardian_kwargs, [{"jobs": 4}])
        # no scenario ran, so no session and no watch report exists
        self.assertFalse(any(self.sessions.iterdir()))
        self.assertFalse(self.reports.exists() and any(self.reports.iterdir()))

    def test_resume_without_incident_json_is_hard_error(self):
        bogus = self.repo / "not-an-incident"
        bogus.mkdir(parents=True)
        with self.assertRaises(SystemExit):
            nw.main(["--resume", str(bogus)])


class MindLegTests(_NightWatchHarness):
    """The mind legs (green review + night synthesis). Direct unit tests use spy
    brain calls; the end-to-end tests pin the backend to handoff and spy the two
    DEFAULT brain-call wrappers, so no test ever performs a real file handoff."""

    def _make_record(self, *, red: bool = False, elapsed: int = 5, run_dir: Path | None = None,
                     scenario: str = "tests/lanista_scenarios/green_one.json",
                     tag: str = "nw-t-001") -> dict:
        return {
            "ts": "2026-08-16T00:00:00+00:00", "scenario": scenario, "tag": tag,
            "exitCode": 1 if red else 0, "elapsedSec": elapsed, "red": red,
            "runDir": str(run_dir) if run_dir else None,
        }

    def _make_run_dir(self, name: str, stderr_lines: list[str]) -> Path:
        run_dir = self.sessions / name
        run_dir.mkdir(parents=True, exist_ok=True)
        (run_dir / "stderr.log").write_text("\n".join(stderr_lines) + "\n", encoding="utf-8")
        return run_dir

    def _green_reviews(self, report_dir: Path) -> list[dict]:
        lines = (report_dir / nw.GREEN_REVIEWS_NAME).read_text(encoding="utf-8").splitlines()
        return [json.loads(line) for line in lines]

    # ---- green review: bundle always, brain optional, honest either way ----

    def test_bundle_and_jsonl_written_even_when_not_attempted(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        record = run_green = nw.run_green_review(
            1, [self._make_record()], report_dir,
            attempt_call=False, skip_reason="backend not handoff",
        )
        bundle = report_dir / "green-review" / "cycle-01.json"
        self.assertTrue(bundle.is_file(), "evidence bundle is the record - always written")
        data = json.loads(bundle.read_text(encoding="utf-8"))
        self.assertEqual(data["cycle"], 1)
        self.assertEqual(len(data["runs"]), 1)
        self.assertEqual(run_green["attempted"], False)
        self.assertIn("backend not handoff", run_green["reason"])
        reviews = self._green_reviews(report_dir)
        self.assertEqual(len(reviews), 1)
        self.assertFalse(reviews[0]["answered"])

    def test_answered_brain_call_records_findings(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        findings = [{"area": "vault", "severity": "low",
                     "evidence": "QRhi context drop x3 in stderr.log"}]
        record = nw.run_green_review(
            1, [self._make_record()], report_dir,
            brain_call=lambda prompt, *, timeout_sec: {
                "findings": findings, "assessment": "solid cycle",
            },
        )
        self.assertTrue(record["attempted"])
        self.assertTrue(record["answered"])
        self.assertEqual(record["findings"], findings)
        self.assertEqual(record["assessment"], "solid cycle")
        self.assertEqual(self._green_reviews(report_dir)[0]["answered"], True)

    def test_invalid_findings_shape_is_recorded_not_fatal(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        record = nw.run_green_review(
            1, [self._make_record()], report_dir,
            brain_call=lambda prompt, *, timeout_sec: {"findings": "not-a-list"},
        )
        self.assertFalse(record["answered"])
        self.assertIn("list of objects", record["invalid"])
        self.assertEqual(len(self._green_reviews(report_dir)), 1, "recorded despite invalid shape")

    def test_brain_invocation_error_is_recorded_not_fatal(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)

        def raise_timeout(prompt, *, timeout_sec):
            raise nw.live_runners.ClaudeInvocationError("handoff timed out after 180s")

        record = nw.run_green_review(1, [self._make_record()], report_dir, brain_call=raise_timeout)
        self.assertFalse(record["answered"])
        self.assertIn("timed out", record["reason"])
        self.assertEqual(len(self._green_reviews(report_dir)), 1)

    def test_signals_extracted_deduped_and_capped(self):
        lines = ["all fine here"] + \
                [f"QRhi warning variant {i}" for i in range(25)] + \
                ["QRhi warning variant 0 again"]
        run_dir = self._make_run_dir("s-run1", lines)
        signals = nw._extract_signals(run_dir)
        self.assertEqual(len(signals), nw._SIGNAL_LINES_PER_RUN, "capped at the per-run bound")
        self.assertNotIn("all fine here", signals)
        self.assertEqual(signals.count("QRhi warning variant 0 again"), 0, "deduped")
        self.assertEqual(nw._extract_signals(None), [])
        empty_dir = self.sessions / "s-run2"
        empty_dir.mkdir()
        self.assertEqual(nw._extract_signals(empty_dir), [], "no stderr.log -> no signals")

    # ---- night synthesis: bundle the night, memo always written ----

    def test_synthesis_bundle_carries_the_whole_night(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        run1 = self._make_run_dir("s-a", ["QRhi failed=3 on boot"])
        run2 = self._make_run_dir("s-b", ["QRhi failed=3 on boot", "QSqlDatabase warning"])
        records = [
            self._make_record(elapsed=10, run_dir=run1),
            self._make_record(elapsed=25, run_dir=run2, red=True,
                              scenario="tests/lanista_scenarios/red_one.json", tag="nw-t-002"),
        ]
        reviews = [{"cycle": 1, "answered": True, "findings": [{"area": "vault"}]}]
        path = nw._write_synthesis_bundle(
            report_dir,
            started_at=nw._utc_now(), ended_at=nw._utc_now(), reason_stopped="1 cycle(s) completed",
            records=records,
            incident_results=[{"opened": True, "incidentId": "AR-2026-08-16-0001", "dir": "x"}],
            guardian_results=[{"terminalState": "DOCUMENTED"}],
            green_reviews=reviews,
        )
        bundle = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(bundle["totals"], {"runs": 2, "reds": 1})
        stats = bundle["perScenario"]["tests/lanista_scenarios/red_one.json"]
        self.assertEqual((stats["runs"], stats["reds"], stats["elapsedMinSec"], stats["elapsedMaxSec"]),
                         (1, 1, 25, 25))
        self.assertEqual(bundle["incidents"][0]["incidentId"], "AR-2026-08-16-0001")
        self.assertEqual(bundle["guardianOutcomes"][0]["terminalState"], "DOCUMENTED")
        self.assertEqual(bundle["greenReviews"], reviews)
        self.assertGreater(bundle["signalHistogram"]["QRhi failed=3 on boot"], 0)
        self.assertEqual(list(bundle["signalHistogram"])[0], "QRhi failed=3 on boot",
                         "histogram sorted by frequency")

    def test_memo_written_from_answer(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        bundle = report_dir / nw.SYNTHESIS_BUNDLE_NAME
        bundle.write_text("{}", encoding="utf-8")
        memo = nw.run_night_synthesis(
            report_dir, bundle,
            brain_call=lambda prompt, *, timeout_sec: {
                "summary": "The app held up.",
                "lacking": [{"area": "reader", "severity": "medium",
                             "evidence": "stderr QRhi x4", "suggestion": "check context loss"}],
                "overall": "Trending cleaner.",
            },
        )
        text = memo.read_text(encoding="utf-8")
        self.assertIn("The app held up.", text)
        self.assertIn("[MEDIUM] reader", text)
        self.assertIn("check context loss", text)
        self.assertIn("Trending cleaner.", text)

    def test_memo_stub_when_not_attempted(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        bundle = report_dir / nw.SYNTHESIS_BUNDLE_NAME
        bundle.write_text("{}", encoding="utf-8")
        memo = nw.run_night_synthesis(
            report_dir, bundle, attempt_call=False, skip_reason="backend not handoff",
        )
        text = memo.read_text(encoding="utf-8")
        self.assertIn("not attempted (backend not handoff)", text)
        self.assertIn(nw.SYNTHESIS_BUNDLE_NAME, text, "stub points at the evidence bundle")
        self.assertIn(nw.GREEN_REVIEWS_NAME, text)

    def test_memo_stub_on_brain_timeout(self):
        report_dir = self.reports / "r1"
        report_dir.mkdir(parents=True)
        bundle = report_dir / nw.SYNTHESIS_BUNDLE_NAME
        bundle.write_text("{}", encoding="utf-8")

        def raise_timeout(prompt, *, timeout_sec):
            raise nw.live_runners.ClaudeInvocationError("no answer before the window closed")

        memo = nw.run_night_synthesis(report_dir, bundle, brain_call=raise_timeout)
        self.assertIn("timeout", memo.read_text(encoding="utf-8"))

    # ---- end to end through main(): the mind runs with the watch, spied ----

    def _spy_mind(self, findings_answer, synthesis_answer):
        self._mind_prompts: list[str] = []
        self._synth_prompts: list[str] = []
        self._saved_green_call = nw._green_review_brain_call
        self._saved_synth_call = nw._synthesis_brain_call

        def green_spy(prompt, *, timeout_sec):
            self._mind_prompts.append(prompt)
            return findings_answer

        def synth_spy(prompt, *, timeout_sec):
            self._synth_prompts.append(prompt)
            return synthesis_answer

        nw._green_review_brain_call = green_spy
        nw._synthesis_brain_call = synth_spy
        nw.live_runners._model_backend = "handoff"

    def _unspy_mind(self):
        nw._green_review_brain_call = self._saved_green_call
        nw._synthesis_brain_call = self._saved_synth_call

    def test_once_watch_runs_green_review_and_synthesis_with_handoff(self):
        self._spy_mind(
            {"findings": [{"area": "vault", "severity": "low", "evidence": "QRhi x2"}],
             "assessment": "clean"},
            {"summary": "One red, one flake.", "lacking": [], "overall": "Stable."},
        )
        try:
            self.assertEqual(self._run_main(), 0)
        finally:
            self._unspy_mind()
        report_dir = self._report_dir()
        reviews = self._green_reviews(report_dir)
        self.assertEqual(len(reviews), 1, "one completed cycle -> one green review")
        self.assertTrue(reviews[0]["answered"])
        self.assertEqual(len(reviews[0]["findings"]), 1)
        bundle = json.loads((report_dir / nw.SYNTHESIS_BUNDLE_NAME).read_text(encoding="utf-8"))
        self.assertEqual(bundle["greenReviews"], reviews)
        memo_text = (report_dir / nw.LACKING_MEMO_NAME).read_text(encoding="utf-8")
        self.assertIn("One red, one flake.", memo_text)
        report = self._report_text()
        self.assertIn("## The mind's record", report)
        self.assertIn(nw.LACKING_MEMO_NAME, report)
        self.assertIn("1/1 answered", report)

    def test_claude_backend_runs_mind_bundle_only_no_calls(self):
        # setUp pins claude and NO spies are installed: if the handoff gate were
        # broken, this test would attempt a real claude/handoff invocation. The
        # assertions prove the gate held without firing anything.
        self.assertEqual(self._run_main(), 0)
        report_dir = self._report_dir()
        reviews = self._green_reviews(report_dir)
        self.assertEqual(len(reviews), 1)
        self.assertFalse(reviews[0]["attempted"])
        self.assertIn("backend not handoff", reviews[0]["reason"])
        memo_text = (report_dir / nw.LACKING_MEMO_NAME).read_text(encoding="utf-8")
        self.assertIn("not attempted", memo_text)

    def test_zero_timeouts_write_bundles_without_calls(self):
        self.assertEqual(self._run_main("--green-review-timeout-sec", "0",
                                        "--synthesis-timeout-sec", "0"), 0)
        report_dir = self._report_dir()
        reviews = self._green_reviews(report_dir)
        self.assertEqual(len(reviews), 1)
        self.assertFalse(reviews[0]["attempted"])
        memo_text = (report_dir / nw.LACKING_MEMO_NAME).read_text(encoding="utf-8")
        self.assertIn("disabled", memo_text)


if __name__ == "__main__":
    unittest.main(verbosity=2)

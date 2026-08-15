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


class NightWatchTests(unittest.TestCase):
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

    def tearDown(self):
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

    def _report_text(self) -> str:
        reports = sorted(self.reports.iterdir())
        self.assertTrue(reports, "no night-watch report dir was written")
        return (reports[-1] / "report.md").read_text(encoding="utf-8")

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


if __name__ == "__main__":
    unittest.main(verbosity=2)

"""P04 runner contract tests.

These tests exercise the standalone lab through its public runner/evidence
interfaces and deliberately use disposable toy subjects.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


TOY_SUBJECT = textwrap.dedent(
    """
    import json, os, socket, subprocess, sys, time
    from pathlib import Path
    mode = sys.argv[1]
    root = os.environ["LAB_RUN_ROOT"]
    if mode == "orphan":
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        Path(root, "grandchild.pid").write_text(str(child.pid), encoding="ascii")
        time.sleep(30)
    if mode in ("timeout", "hold"):
        if mode == "timeout":
            child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            Path(root, "descendant.pid").write_text(str(child.pid), encoding="ascii")
        if mode == "hold":
            with socket.create_connection(("127.0.0.1", int(os.environ["LAB_PORT"])), timeout=2):
                Path(root, "listener-connected").write_text("yes", encoding="ascii")
        time.sleep(30)
    if mode == "crash":
        raise SystemExit(7)
    if mode != "missing-evidence":
        response = {
            "status": 500 if mode == "status" else 200,
            "headers": {"Content-Type": "text/plain" if mode == "header" else "application/json"},
            "body": "wrong" if mode == "byte" else "expected",
        }
        Path(root, "subject-response.json").write_text(json.dumps(response), encoding="utf-8")
        protocol_status = 500 if mode in ("status", "raw-diverge") else 200
        protocol_type = "text/plain" if mode in ("header", "raw-diverge") else "application/json"
        protocol_body = "wrong" if mode in ("byte", "raw-diverge") else "expected"
        Path(root, "protocol-response.txt").write_bytes(
            f"HTTP/1.1 {protocol_status} OK\\r\\nContent-Type: {protocol_type}\\r\\nX-Test: exact\\r\\n\\r\\n{protocol_body}".encode("latin-1")
        )
        """
)


class P04RunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="p04-runner-test-")
        self.root = Path(self.temp.name)
        self.subject = self.root / "toy_subject.py"
        self.subject.write_text(TOY_SUBJECT, encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def run_cli(self, mode: str, run_id: str = "run-1") -> tuple[int, dict, Path]:
        evidence = self.root / "evidence" / run_id
        completed = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.server_lab.lab",
                "--subject",
                str(self.subject),
                "--mode",
                mode,
                "--data-root",
                str(self.root / "data"),
                "--evidence-dir",
                str(evidence),
                "--run-id",
                run_id,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            env={**os.environ, "PYTHONPATH": str(ROOT)},
        )
        receipt = json.loads((evidence / "run.json").read_text(encoding="utf-8"))
        return completed.returncode, receipt, evidence

    def test_broken_subjects_have_nonzero_exit_for_all_p04_01_failures(self) -> None:
        expected = {
            "byte": ("FAIL", "byte mismatch"),
            "status": ("FAIL", "status mismatch"),
            "header": ("FAIL", "header mismatch"),
            "timeout": ("ERROR", "timed out"),
            "crash": ("ERROR", "exited with"),
            "missing-evidence": ("INDETERMINATE", "missing"),
        }
        for mode, (result, evidence) in expected.items():
            with self.subTest(mode=mode):
                exit_code, receipt, _ = self.run_cli(mode, f"run-{mode}")
                self.assertNotEqual(exit_code, 0)
                self.assertEqual(receipt["result"], result)
                self.assertTrue(any(evidence in item for item in receipt["errors"]))
                self.assertIn("raw_lane", receipt)
                self.assertIn("normalized_lane", receipt)
                if mode == "timeout":
                    descendant = Path(receipt["paths"]["run_root"]) / "descendant.pid"
                    if descendant.exists():
                        self.assertFalse(_pid_alive(int(descendant.read_text(encoding="ascii"))))

    def test_p04_02_statuses_remain_distinct_and_never_pass(self) -> None:
        from tools.server_lab.evidence import EvidenceSchema

        statuses = ("FAIL", "ERROR", "UNSUPPORTED", "INDETERMINATE", "NOT_RUN")
        for status in statuses:
            with self.subTest(status=status):
                receipt = EvidenceSchema.receipt(
                    run_id=f"status-{status.lower()}",
                    engine={"name": "toy", "version": "1"},
                    source={"identity": "toy"},
                    scenario="classification",
                    environment={},
                    configuration={},
                    fixture_identifiers={},
                    request_sequence=[],
                    timestamps={},
                    raw_lane={},
                    normalized_lane={},
                    observations=[],
                    errors=[],
                    result=status,
                    replay={"command": ["toy"]},
                )
                self.assertEqual(receipt["result"], status)
                self.assertNotEqual(receipt["result"], "PASS")

    def test_interleaved_runs_isolate_cache_ports_events_and_cleanup_orphans(self) -> None:
        first_evidence = self.root / "evidence" / "run-a"
        first_runner = subprocess.Popen(
            [sys.executable, "-m", "tools.server_lab.lab", "--subject", str(self.subject), "--mode", "hold", "--data-root", str(self.root / "data"), "--evidence-dir", str(first_evidence), "--run-id", "run-a"],
            cwd=ROOT, env={**os.environ, "PYTHONPATH": str(ROOT)},
        )
        try:
            first_root = self.root / "data" / "run-a"
            marker = first_root / "listener-connected"
            lease = first_root / "ownership.json"
            deadline = time.time() + 3
            while time.time() < deadline and (not marker.exists() or not lease.exists()):
                time.sleep(0.02)
            self.assertTrue(marker.exists())
            self.assertTrue(lease.exists())
            with ThreadPoolExecutor(max_workers=1) as pool:
                second_future = pool.submit(self.run_cli, "hold", "run-b")
                time.sleep(0.4)
                self.assertIsNone(first_runner.poll(), "run B startup cleanup reaped live run A")
                second_exit, second, _ = second_future.result()
            first_runner.wait(timeout=5)
            first_exit = first_runner.returncode
            first = json.loads((first_evidence / "run.json").read_text(encoding="utf-8"))
        finally:
            if first_runner.poll() is None:
                first_runner.kill()
                first_runner.wait(timeout=3)
        self.assertNotEqual(first_exit, 0)
        self.assertNotEqual(second_exit, 0)
        self.assertNotEqual(first["run_id"], second["run_id"])
        self.assertNotEqual(first["paths"]["run_root"], second["paths"]["run_root"])
        self.assertNotEqual(first["paths"]["port"], second["paths"]["port"])
        self.assertTrue(first["observations"][0]["listener_owned"])
        self.assertTrue(second["observations"][0]["listener_owned"])
        self.assertRegex(first["paths"]["endpoint"], r"^127\.0\.0\.1:\d+$")
        self.assertNotEqual(first["paths"]["event_file"], second["paths"]["event_file"])
        self.assertNotEqual(first["paths"]["cache"], second["paths"]["cache"])
        self.assertEqual(first["cleanup"]["owned_children_after"], [])
        self.assertEqual(second["cleanup"]["owned_children_after"], [])

        runner_root = self.root / "data" / "run-orphan"
        evidence = self.root / "evidence" / "run-orphan"
        from tools.server_lab.lab import cleanup_orphans
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        orphan_runner = None
        child_pid_file = runner_root / "grandchild.pid"
        try:
            orphan_runner = subprocess.Popen(
                [sys.executable, "-m", "tools.server_lab.lab", "--subject", str(self.subject), "--mode", "orphan", "--data-root", str(self.root / "data"), "--evidence-dir", str(evidence), "--run-id", "run-orphan"],
                cwd=ROOT,
                env={**os.environ, "PYTHONPATH": str(ROOT)},
            )
            deadline = time.time() + 3
            while time.time() < deadline and (not child_pid_file.exists() or not child_pid_file.read_text(encoding="ascii").strip()):
                time.sleep(0.02)
            self.assertTrue(child_pid_file.exists())
            grandchild_pid = int(child_pid_file.read_text(encoding="ascii"))
            ownership = runner_root / "ownership.json"
            deadline = time.time() + 3
            while time.time() < deadline and not ownership.exists():
                time.sleep(0.02)
            lease = json.loads(ownership.read_text(encoding="utf-8"))
            self.assertTrue(set(("token", "pid", "command", "started_at", "identity")) <= set(lease))
            subject_pid = int(lease["pid"])
            orphan_runner.kill()
            orphan_runner.wait(timeout=3)
            cleanup = cleanup_orphans(self.root / "data")
            self.assertIn(subject_pid, cleanup["removed_pids"])
            self.assertIn(subject_pid, cleanup["verified_pids"])
            self.assertIn(str(runner_root), cleanup["orphaned_runs"])
            self.assertFalse(_pid_alive(subject_pid))
            self.assertFalse(_pid_alive(grandchild_pid))
            self.assertTrue(_pid_alive(unrelated.pid))

            mismatch_root = self.root / "data" / "run-mismatch"
            mismatch_root.mkdir()
            from tools.server_lab.lab import _process_identity
            unrelated_identity = _process_identity(unrelated.pid) or {}
            (mismatch_root / "ownership.json").write_text(json.dumps({
                "token": "mismatch", "pid": unrelated.pid, "command": ["not-owned"], "started_at": "now",
                "identity": {"subject": str(self.subject), "mode": "orphan", "token": "mismatch", "command": "not-owned", "creation": str(unrelated_identity.get("creation", "wrong")) + "-wrong"}
            }), encoding="utf-8")
            mismatch = cleanup_orphans(self.root / "data")
            self.assertIn(str(mismatch_root), mismatch["skipped_mismatches"])
            self.assertTrue(_pid_alive(unrelated.pid))
            stale_root = self.root / "data" / "run-stale"
            stale_root.mkdir()
            (stale_root / "ownership.json").write_text(json.dumps({
                "token": "stale", "pid": 99999999, "command": ["not-running"], "started_at": "now",
                "identity": {"subject": str(self.subject), "mode": "orphan", "token": "stale", "command": "not-running", "creation": "gone"}
            }), encoding="utf-8")
            stale = cleanup_orphans(self.root / "data")
            self.assertIn(str(stale_root), stale["stale_leases"])
            self.assertNotIn(99999999, stale["removed_pids"])
        finally:
            if orphan_runner is not None and orphan_runner.poll() is None:
                orphan_runner.kill()
                orphan_runner.wait(timeout=3)
            cleanup_orphans(self.root / "data")
            if child_pid_file.exists() and child_pid_file.read_text(encoding="ascii").strip():
                leaked = int(child_pid_file.read_text(encoding="ascii"))
                if _pid_alive(leaked):
                    subprocess.run(["taskkill", "/PID", str(leaked), "/T", "/F"], capture_output=True, check=False)
            if unrelated.poll() is None:
                unrelated.kill()
                unrelated.wait(timeout=3)

    def test_schema_round_trip_and_raw_normalized_lanes_are_separate(self) -> None:
        exit_code, receipt, evidence = self.run_cli("byte", "run-roundtrip")
        self.assertNotEqual(exit_code, 0)
        schema = json.loads((ROOT / "tools/server_lab/schemas/run.schema.json").read_text(encoding="utf-8"))
        encoded = json.dumps(receipt)
        decoded = json.loads(encoded)
        self.assertEqual(decoded, json.loads((evidence / "run.json").read_text(encoding="utf-8")))
        from tools.server_lab.evidence import EvidenceSchema
        EvidenceSchema.validate(decoded, schema)
        invalid = dict(decoded)
        invalid.pop("configuration")
        with self.assertRaises(ValueError):
            EvidenceSchema.validate(invalid, schema)
        required = schema["required"]
        self.assertTrue(set(required).issubset(decoded))
        expected_types = {
            "run_id": str, "engine": dict, "source": dict, "scenario": str,
            "environment": dict, "configuration": dict, "fixture_identifiers": dict,
            "request_sequence": list, "timestamps": dict, "raw_lane": dict,
            "normalized_lane": dict, "observations": list, "errors": list,
            "result": str, "replay": dict,
        }
        for field, field_type in expected_types.items():
            self.assertIsInstance(decoded[field], field_type, field)
        self.assertEqual(decoded["schema"], schema["properties"]["schema"]["const"])
        self.assertIn(decoded["result"], schema["properties"]["result"]["enum"])
        self.assertIn("exit_code", receipt["raw_lane"])
        self.assertEqual(receipt["raw_lane"]["protocol_text"], "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Test: exact\r\n\r\nwrong")
        self.assertEqual(bytes.fromhex(receipt["raw_lane"]["protocol_bytes_hex"]).decode("latin-1"), receipt["raw_lane"]["protocol_text"])
        self.assertEqual(receipt["raw_lane"]["protocol_status"], 200)
        self.assertEqual(receipt["raw_lane"]["protocol_headers"]["Content-Type"], "application/json")
        self.assertNotIn("exit_code", receipt["normalized_lane"])
        self.assertIn("response", receipt["normalized_lane"])
        self.assertNotIn("response", receipt["raw_lane"])
        Path(receipt["paths"]["stdout"]).unlink()
        Path(receipt["paths"]["stderr"]).unlink()
        self.assertFalse(Path(receipt["paths"]["stdout"]).exists())
        self.assertIn("startup_cleanup", receipt)
        self.assertFalse(receipt["observations"][0]["listener_owned"])

    def test_raw_protocol_divergence_cannot_be_normalized_into_pass(self) -> None:
        exit_code, receipt, _ = self.run_cli("raw-diverge", "run-raw-diverge")
        self.assertNotEqual(exit_code, 0)
        self.assertEqual(receipt["result"], "FAIL")
        self.assertIn("raw protocol divergence", " ".join(receipt["errors"]))

    def test_cleanup_cli_is_usable_without_subject_arguments(self) -> None:
        completed = subprocess.run(
            [sys.executable, "-m", "tools.server_lab.lab", "--cleanup", "--data-root", str(self.root / "data")],
            cwd=ROOT, capture_output=True, text=True, env={**os.environ, "PYTHONPATH": str(ROOT)},
        )
        self.assertEqual(completed.returncode, 0)

    def test_event_is_written_after_final_result_and_matches_receipt(self) -> None:
        from unittest.mock import patch
        from tools.server_lab.lab import LabRunner
        subject = self.root / "event-order.py"
        subject.write_text(textwrap.dedent("""
            import json, os
            from pathlib import Path
            root = Path(os.environ['LAB_RUN_ROOT'])
            response = {'status': 200, 'headers': {'Content-Type': 'application/json'}, 'body': 'expected'}
            (root / 'subject-response.json').write_text(json.dumps(response), encoding='utf-8')
            (root / 'protocol-response.txt').write_text('HTTP/1.1 200 OK\\r\\nContent-Type: application/json\\r\\n\\r\\nexpected', encoding='latin-1')
            (root / 'descendant.pid').write_text('4242', encoding='ascii')
        """), encoding="utf-8")
        with patch("tools.server_lab.lab._process_identity", side_effect=lambda pid: {"command": "owned", "creation": "same"} if pid == 4242 else None):
            receipt = LabRunner().run(subject=subject, mode="pass", data_root=self.root / "data", evidence_dir=self.root / "evidence" / "event-order", run_id="run-event-order")
        self.assertEqual(receipt["result"], "ERROR")
        events = [json.loads(line) for line in Path(receipt["paths"]["event_file"]).read_text(encoding="utf-8").splitlines()]
        self.assertEqual(events[-1]["run_id"], receipt["run_id"])
        self.assertEqual(events[-1]["result"], "ERROR")
        self.assertEqual(events[-1]["result"], receipt["result"])

    def test_replay_is_full_lab_command_and_uses_committed_fixture(self) -> None:
        fixture = ROOT / "artifacts" / "server1" / "P04" / "fixtures" / "replay_subject.py"
        config = ROOT / "artifacts" / "server1" / "P04" / "fixtures" / "replay-config.json"
        evidence = self.root / "evidence" / "replay-source"
        completed = subprocess.run(
            [sys.executable, "-m", "tools.server_lab.lab", "--config", str(config), "--subject", str(fixture), "--data-root", str(self.root / "data"), "--evidence-dir", str(evidence), "--run-id", "replay-source"],
            cwd=ROOT, capture_output=True, text=True, env={**os.environ, "PYTHONPATH": str(ROOT)},
        )
        receipt = json.loads((evidence / "run.json").read_text(encoding="utf-8"))
        self.assertEqual(receipt["replay"]["exact_command"][:4], [sys.executable, "-m", "tools.server_lab.lab", "--config"])
        self.assertIn(str(fixture), receipt["replay"]["exact_command"])
        self.assertEqual(receipt["replay"]["required_substitutions"], ["data_root", "evidence_dir", "run_id"])
        replay_evidence = self.root / "evidence" / "replay-copy"
        substitutions = {"data_root": str(self.root / "data"), "evidence_dir": str(replay_evidence), "run_id": "replay-copy"}
        replay_command = [arg.format(**substitutions) for arg in receipt["replay"]["command_template"]]
        replayed = subprocess.run(replay_command, cwd=ROOT, capture_output=True, text=True, env={**os.environ, "PYTHONPATH": str(ROOT)})
        self.assertEqual(replayed.returncode, completed.returncode)
        self.assertEqual(json.loads((replay_evidence / "run.json").read_text(encoding="utf-8"))["result"], receipt["result"])

    def test_receipt_has_nullable_server_fields_and_observation_arrays(self) -> None:
        _, receipt, _ = self.run_cli("byte", "run-fields")
        for field in ("torrent", "infohash", "selected_file"):
            self.assertIn(field, receipt)
            self.assertIsNone(receipt[field])
        for field in ("responses", "byte_counts", "peer_observations", "resource_observations"):
            self.assertIn(field, receipt)
            self.assertIsInstance(receipt[field], list)

    def test_cleanup_failure_cannot_pass(self) -> None:
        from unittest.mock import patch
        from tools.server_lab.lab import LabRunner
        with patch("tools.server_lab.lab._process_identity", side_effect=lambda pid: {"command": "owned", "creation": "same"} if pid == 4242 else None):
            subject = self.root / "cleanup-failure.py"
            subject.write_text(textwrap.dedent("""
                import json, os
                from pathlib import Path
                root = Path(os.environ['LAB_RUN_ROOT'])
                response = {'status': 200, 'headers': {'Content-Type': 'application/json'}, 'body': 'expected'}
                (root / 'subject-response.json').write_text(json.dumps(response), encoding='utf-8')
                (root / 'protocol-response.txt').write_text('HTTP/1.1 200 OK\\r\\nContent-Type: application/json\\r\\n\\r\\nexpected', encoding='latin-1')
                (root / 'descendant.pid').write_text('4242', encoding='ascii')
            """), encoding="utf-8")
            receipt = LabRunner().run(subject=subject, mode="pass", data_root=self.root / "data", evidence_dir=self.root / "evidence" / "cleanup-failure", run_id="cleanup-failure")
        self.assertNotEqual(receipt["result"], "PASS")
        self.assertIn("cleanup", " ".join(receipt["errors"]).lower())


def _pid_alive(pid: int) -> bool:
    completed = subprocess.run(["tasklist", "/FI", f"PID eq {pid}"], capture_output=True, text=True, check=False)
    return str(pid) in completed.stdout


if __name__ == "__main__":
    unittest.main()

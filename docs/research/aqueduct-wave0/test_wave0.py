import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ORACLE = ROOT / "oracle"
sys.path.insert(0, str(ORACLE))
import capture_oracle  # noqa: E402
sys.path.insert(0, str(ROOT))


class Wave0HarnessContractTests(unittest.TestCase):
    def test_capture_script_exposes_all_three_suites(self):
        self.assertTrue(callable(capture_oracle.control_suite))
        self.assertTrue(callable(capture_oracle.offline_suite))
        self.assertTrue(callable(capture_oracle.live_suite))

    def test_capture_script_has_cli_entrypoint(self):
        self.assertTrue(callable(capture_oracle.main))


class CandidateComparatorTests(unittest.TestCase):
    def test_equal_capture_documents_compare_green(self):
        import compare_candidate
        sample = {
            "suite": "control",
            "startup_handshake_seen": True,
            "shutdown_clean": True,
            "records": [{"name": "heartbeat", "response": {"status": 200}}],
        }
        self.assertEqual(compare_candidate.compare_documents(sample, sample), [])

    def test_status_mismatch_is_reported(self):
        import compare_candidate
        golden = {
            "suite": "control",
            "records": [{"name": "heartbeat", "response": {"status": 200}}],
        }
        candidate = {
            "suite": "control",
            "records": [{"name": "heartbeat", "response": {"status": 500}}],
        }
        failures = compare_candidate.compare_documents(golden, candidate)
        self.assertTrue(any("status" in failure for failure in failures))


class CleanupRetryTests(unittest.TestCase):
    def test_remove_tree_retries_transient_permission_error(self):
        from unittest import mock
        target = ROOT
        with mock.patch.object(capture_oracle.shutil, "rmtree", side_effect=[PermissionError("locked"), None]) as remove:
            with mock.patch.object(capture_oracle.time, "sleep"):
                capture_oracle.remove_tree_with_retries(target, attempts=2, delay=0.01)
        self.assertEqual(remove.call_count, 2)


class ProcessTreeCleanupTests(unittest.TestCase):
    def test_windows_process_tree_uses_taskkill(self):
        from unittest import mock
        process = mock.Mock(pid=1234)
        process.poll.return_value = None
        process.wait.return_value = 0
        with mock.patch.object(capture_oracle.os, "name", "nt"):
            with mock.patch.object(capture_oracle.subprocess, "run") as run:
                capture_oracle.terminate_process_tree(process)
        run.assert_called_once()
        self.assertIn("/T", run.call_args.args[0])


class Wave0VerifierTests(unittest.TestCase):
    def test_current_wave0_artifacts_validate(self):
        import verify_wave0
        self.assertEqual(verify_wave0.validate(ROOT), [])


if __name__ == "__main__":
    unittest.main()

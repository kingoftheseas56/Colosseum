"""Contract checks for failure/no-assertion rejection. No Qt run is simulated as evidence."""
import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
spec = importlib.util.spec_from_file_location("runner", Path(__file__).with_name("feria_navigation_smoke_test.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class RunnerContract(unittest.TestCase):
    def result(self, code, raw, minimum=2):
        with patch.object(runner, "run_process", return_value=subprocess.CompletedProcess([], code, raw)):
            return runner.run_check(["fixture"], {}, minimum, 1, "runner-contract", None)
    def test_zero_exit_without_assertions_fails(self):
        self.assertFalse(self.result(0,b"loaded successfully\n"))
    def test_low_assertion_count_fails(self):
        self.assertFalse(self.result(0,b'{"success":true}\n'))
    def test_failed_assertion_fails(self):
        self.assertFalse(self.result(0,b'{"success":true}\n{"success":false}\n'))
    def test_nonzero_with_assertions_fails(self):
        self.assertFalse(self.result(1,b'{"success":true}\n{"success":true}\n'))
    def test_complete_assertions_pass(self):
        self.assertTrue(self.result(0,b'{"success":true}\n{"success":true}\n'))
    def test_prior_minimums_preserved(self):
        self.assertEqual(runner.SMOKES["FeriaDirectionalNavigationSmoke"],125)
        self.assertEqual(runner.SMOKES["FeriaRestorationEdgesSmoke"],34)
        self.assertEqual(runner.SMOKES["FeriaRecoverySmoke"],40)
        self.assertEqual(runner.SMOKES["FeriaRecoveryCatalogueSmoke"],20)

    def test_windows_timeout_kills_owned_tree_and_uses_file_output(self):
        process = Mock(pid=123)
        process.wait.side_effect = [subprocess.TimeoutExpired(["fixture"], 1), 0]
        process.poll.return_value = None
        with patch.object(runner.os, "name", "nt"), \
                patch.object(runner.subprocess, "Popen", return_value=process) as launch, \
                patch.object(runner.subprocess, "run") as terminate:
            result = runner.run_process(["fixture"], {}, 1)
        self.assertEqual(result.returncode, "timeout")
        self.assertNotEqual(launch.call_args.kwargs["stdout"], subprocess.PIPE)
        terminate.assert_called_once_with(['taskkill', '/PID', '123', '/T', '/F'],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                         timeout=10, check=False)
        process.kill.assert_called_once()

if __name__ == "__main__":
    unittest.main()

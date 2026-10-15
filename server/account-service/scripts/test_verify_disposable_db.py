"""Fail-closed regression: rejected inputs must never invoke fixture tools."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

RUNNER = Path(__file__).with_name("verify-disposable-db.py")


class GuardTests(unittest.TestCase):
    def test_external_configuration_rejected_before_tools(self):
        cases = (
            {"TEST_DATABASE_URL": ""},
            {"TEST_DATABASE_URL": "postgres://invalid/production"},
            {"TEST_DATABASE_URL": "postgres://invalid/production_test"},
            {"DATABASE_URL": "postgres://invalid/production"},
            {"MIGRATION_DATABASE_URL": "postgres://invalid/production"},
            {"PGHOST": "invalid"},
            {"PGSERVICE": "production"},
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker = root / "tool-called"
            for tool in ("initdb", "pg_ctl", "createdb", "psql", "go"):
                path = root / tool
                path.write_text(f"#!/bin/sh\ntouch '{marker}'\nexit 99\n")
                path.chmod(0o755)
            for case in cases:
                with self.subTest(case=list(case.items())):
                    env = {"PATH": directory, **case}
                    result = subprocess.run([sys.executable, str(RUNNER)], env=env, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("refusing external database", result.stderr)
                    self.assertNotIn("evidence:", result.stdout)
                    self.assertFalse(marker.exists())

    def test_missing_tools_fail_before_provisioning(self):
        spec = importlib.util.spec_from_file_location("fixture_runner", RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(sys, "argv", [str(RUNNER)]), \
                mock.patch.object(module.os, "geteuid", return_value=1000), \
                mock.patch.object(module.shutil, "which", return_value=None), \
                mock.patch.object(module.tempfile, "mkdtemp") as allocate:
            with self.assertRaisesRegex(SystemExit, "missing required tool: initdb"):
                module.main()
            allocate.assert_not_called()

    def test_arguments_cannot_supply_target(self):
        result = subprocess.run([sys.executable, str(RUNNER), "postgres://invalid/x_test"],
                                env={"PATH": ""}, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refusing external database", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

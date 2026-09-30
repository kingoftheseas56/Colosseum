"""Linux clean-container startup regressions; no Docker/X11 runtime claim.

Run: python3 -m unittest -v tests.test_linux_runtime_start
"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/linux_release/qualify.py'

@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux qualification process semantics')
class StartupEvidence(unittest.TestCase):
    def test_setup_failure_keeps_fail_closed_report(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            env = {k: v for k, v in os.environ.items() if k != 'DISPLAY'}
            result = subprocess.run([sys.executable, str(SCRIPT), '--appdir', str(root/'app'), '--evidence', str(root/'evidence'), '--fixture', str(root/'fixture')], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertTrue((root/'evidence/qualification.json').is_file(), 'setup exception lost all qualification evidence')
            report = json.loads((root/'evidence/qualification.json').read_text())
            self.assertFalse(report['qualified'])
            self.assertEqual(report['checks']['setup'], 'FAIL')
            self.assertIn('DISPLAY', report['error'])

    def test_sigterm_keeps_failed_active_phase(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root/'app'
            (app/'usr/bin').mkdir(parents=True)
            for name in ['ffmpeg', 'ffprobe', 'bsdtar']:
                path = app/'usr/bin'/name
                path.write_text('#!/bin/sh\nexit 0\n')
                path.chmod(0o755)
            launcher = app/'AppRun'
            launcher.write_text('#!/bin/sh\necho $$ > \"$HOME/app-started\"\nexec sleep 30\n')
            launcher.chmod(0o755)
            evidence = root/'evidence'
            process = subprocess.Popen([sys.executable, str(SCRIPT), '--appdir', str(app), '--evidence', str(evidence), '--fixture', str(root/'fixture')], env=dict(os.environ, DISPLAY=':99'), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 5
                while not (evidence/'profile/app-started').exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue((evidence/'profile/app-started').exists())
                process.terminate()
                stdout, stderr = process.communicate(timeout=12)
                self.assertEqual(process.returncode, 1, stderr)
                report = json.loads((evidence/'qualification.json').read_text())
                self.assertFalse(report['qualified'])
                self.assertEqual(report['checks']['launch'], 'FAIL')
                self.assertIn('signal 15', report['error'])
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

    def test_ldd_timeout_preserves_current_elf_path(self):
        spec = importlib.util.spec_from_file_location('qualify', SCRIPT)
        q = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(q)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            elf = root/'libsample.so'
            elf.write_bytes(b'\x7fELF\x02\x01\x01' + bytes(9) + (3).to_bytes(2, 'little') + bytes(46))
            with patch.object(q.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['ldd', str(elf)], 30)):
                with self.assertRaises(subprocess.TimeoutExpired):
                    q.closure(root, root, {})
            self.assertIn('libsample.so', (root/'elf-closure.log').read_text())

    def test_clean_launch_keeps_init_and_security_boundary(self):
        script = (ROOT/'scripts/linux_release/clean.sh').read_text()
        launch = next(line for line in script.splitlines() if line.startswith('docker run '))
        self.assertIn('--init', launch.split())
        self.assertIn('--cap-drop=ALL', launch.split())
        self.assertIn('--security-opt=no-new-privileges', launch.split())
        image = (ROOT / 'scripts/linux_release/clean-runtime.Dockerfile').read_text()
        self.assertIn('USER 1000:1000', image)
        self.assertIn('-e /evidence/xvfb.log', script)
        self.assertIn('timeout --kill-after=10s 20m', script)
        self.assertIn('qualification.json', script)
        self.assertIn('startup.log', script)
        self.assertNotIn('--no-sandbox', script)


@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux shell timeout/process semantics')
class WrapperTests(unittest.TestCase):
    def run_wrapper(self, behavior):
        # Execute the actual container shell body, replacing only Xvfb's external
        # process boundary and shortening deadlines. This exercises supervision
        # and evidence behavior without requiring Docker or any display socket.
        # The fake Xvfb does not establish actual GUI/runtime qualification.
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        evidence = root/'evidence'
        evidence.mkdir()
        tools = root/'bin'
        tools.mkdir()
        script = (ROOT/'scripts/linux_release/clean.sh').read_text().split("<<'CONTAINER'\n", 1)[1].rsplit('CONTAINER', 1)[0]
        script = script.replace('/evidence', str(evidence)).replace('seq 1 60', 'seq 1 5')
        script = script.replace('sleep 1', 'sleep 0.02').replace('20m', '1s')
        xvfb = tools/'xvfb-run'
        xvfb.write_text('#!/bin/sh\n'+behavior.replace('EVIDENCE', str(evidence))+'\n')
        xvfb.chmod(0o755)
        env = dict(os.environ, PATH=str(tools)+':'+os.environ['PATH'])
        result = subprocess.run(['sh'], input=script, text=True, capture_output=True, env=env, timeout=15)
        return result, evidence

    def test_success_requires_python_start_marker(self):
        result, evidence = self.run_wrapper("printf '{}\\n' > EVIDENCE/qualification.json; exit 0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('qualifier: initialized', (evidence/'startup.log').read_text())
        self.assertIn('exit=0', (evidence/'startup.log').read_text())

    def test_xvfb_exit_before_python_keeps_diagnostics(self):
        result, evidence = self.run_wrapper('exit 4')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('did not initialize', (evidence/'startup.log').read_text())
        self.assertTrue((evidence/'startup-processes.txt').is_file())
        self.assertFalse((evidence/'qualification.json').exists())

    def test_xvfb_hang_before_python_is_bounded(self):
        result, evidence = self.run_wrapper('exec sleep 30')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('exit=1', (evidence/'startup.log').read_text())

    def test_runtime_timeout_keeps_nonzero_status(self):
        result, evidence = self.run_wrapper("printf '{}\\n' > EVIDENCE/qualification.json; exec sleep 30")
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertIn('exit=124', (evidence/'startup.log').read_text())


if __name__ == '__main__':
    unittest.main()

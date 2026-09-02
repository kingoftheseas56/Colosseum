import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "linux_runtime_dependency_gate.py"


class LinuxRuntimeDependencyGateTest(unittest.TestCase):
    def run_gate(self, appdir: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--appdir", str(appdir), *extra],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def seed_core(appdir: Path) -> None:
        libdir = appdir / "usr" / "lib"
        libdir.mkdir(parents=True, exist_ok=True)
        (libdir / "libxcb-cursor.so.0").write_bytes(b"fixture")
        (libdir / "libmpv.so.2").write_bytes(b"fixture")

    def test_appdir_rejects_missing_xcb_cursor_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp)
            libdir = appdir / "usr" / "lib"
            libdir.mkdir(parents=True)
            (libdir / "libmpv.so.2").write_bytes(b"fixture")

            result = self.run_gate(appdir)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("libxcb-cursor.so.0=MISSING", result.stdout)

    def test_appdir_rejects_misplaced_xcb_cursor_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp)
            self.seed_core(appdir)
            (appdir / "usr" / "lib" / "libxcb-cursor.so.0").unlink()
            bad = appdir / "usr" / "share" / "payload" / "libxcb-cursor.so.0"
            bad.parent.mkdir(parents=True)
            bad.write_bytes(b"fixture")

            result = self.run_gate(appdir)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("libxcb-cursor.so.0=MISSING", result.stdout)

    def test_missing_mpv_cli_is_explicit_fail_closed_by_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp)
            self.seed_core(appdir)

            result = self.run_gate(appdir)

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("dvr_mpv=FAIL_CLOSED", result.stdout)
            self.assertIn("Player1 libmpv remains available", result.stdout)

    def test_require_dvr_rejects_missing_mpv_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp)
            self.seed_core(appdir)

            result = self.run_gate(appdir, "--require-dvr")

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dvr_mpv=MISSING", result.stdout)

    def test_require_dvr_accepts_bundled_executable_mpv(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp)
            self.seed_core(appdir)
            mpv = appdir / "usr" / "bin" / "mpv"
            mpv.parent.mkdir(parents=True)
            mpv.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            mpv.chmod(0o755)

            result = self.run_gate(appdir, "--require-dvr")

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("dvr_mpv=AVAILABLE", result.stdout)


if __name__ == "__main__":
    unittest.main()

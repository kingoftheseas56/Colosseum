"""Exercise install_toolchain.sh cache repair without downloads or an SDK."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ToolchainInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.toolchain = self.root / "toolchain"
        self.log = self.root / "calls.jsonl"
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        COLOSSEUM_ANDROID_TOOLCHAIN=str(self.toolchain),
                        QT_ROOT=str(self.toolchain / "Qt"), QT_VERSION="6.11.1",
                        SKIP_SDK="1", SKIP_QT="0", TEST_AQT_LOG=str(self.log))
        java = self.bin / "java"
        java.write_text('#!/bin/sh\necho \'openjdk version "21.0.1"\' >&2\n')
        java.chmod(0o755)
        aqt = self.bin / "aqt"
        aqt.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
args = sys.argv[1:]
with open(os.environ["TEST_AQT_LOG"], "a") as out:
    out.write(json.dumps(args) + "\\n")
if os.environ.get("TEST_AQT_BROKEN") == "1":
    sys.exit(0)
kit = pathlib.Path(args[args.index("-O") + 1]) / args[3] / args[4].removeprefix("linux_")
(kit / "bin").mkdir(parents=True, exist_ok=True)
for name in ("qmake", "qt-cmake"):
    executable = kit / "bin" / name
    executable.touch()
    executable.chmod(0o755)
for module in ("Qt6Quick3D", "Qt6ShaderTools", "Qt6QuickTimeline"):
    path = kit / "lib/cmake" / module / (module + "Config.cmake")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
for module in ("QtQuick3D", "QtQuick3D/Helpers"):
    path = kit / "qml" / module / "qmldir"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
''')
        aqt.chmod(0o755)

    def run_installer(self):
        return subprocess.run(["bash", str(ROOT / "scripts/android/install_toolchain.sh")],
                              env=self.env, capture_output=True, text=True, timeout=20)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_fresh_install_then_idempotent_reuse(self):
        result = self.run_installer()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(3, len(self.calls()))
        for call in self.calls():
            self.assertTrue({"qtquick3d", "qtshadertools", "qtquicktimeline"} <= set(call))
        self.assertEqual(0, self.run_installer().returncode)
        self.assertEqual(3, len(self.calls()))

    def test_old_base_only_kits_are_repaired(self):
        for arch in ("gcc_64", "android_arm64_v8a", "android_x86_64"):
            directory = self.toolchain / "Qt/6.11.1" / arch / "bin"
            directory.mkdir(parents=True)
            for name in ("qmake", "qt-cmake"):
                (directory / name).touch()
                (directory / name).chmod(0o755)
        result = self.run_installer()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(3, len(self.calls()))

    def test_partial_target_module_cache_is_repaired_only_for_that_kit(self):
        self.assertEqual(0, self.run_installer().returncode)
        (self.toolchain / "Qt/6.11.1/android_x86_64/qml/QtQuick3D/Helpers/qmldir").unlink()
        result = self.run_installer()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(4, len(self.calls()))
        self.assertEqual("android_x86_64", self.calls()[-1][4])

    def test_successful_installer_without_modules_cannot_pass(self):
        self.env["TEST_AQT_BROKEN"] = "1"
        result = self.run_installer()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("missing required Quick3D modules", result.stderr)


if __name__ == "__main__":
    unittest.main()

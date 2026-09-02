import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "android"))

import qualify_toolchain as gate  # noqa: E402


class AndroidToolchainQualificationTests(unittest.TestCase):
    def test_version_tuple(self):
        self.assertEqual(gate.version_tuple("cmake version 3.30.5"), (3, 30, 5))
        self.assertEqual(gate.version_tuple("no version"), ())

    def test_java_major(self):
        self.assertEqual(gate.java_major('openjdk version "21.0.8" 2026-07-15'), 21)
        self.assertEqual(gate.java_major('java version "1.8.0_402"'), 8)
        self.assertIsNone(gate.java_major("not java output"))

    def test_ndk_revision(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "source.properties").write_text(
                "Pkg.Desc = Android NDK\nPkg.Revision = 27.2.12479018\n",
                encoding="utf-8",
            )
            self.assertEqual(gate.ndk_revision(root), gate.NDK_REVISION)

    def test_sdk_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "platforms" / "android-36").mkdir(parents=True)
            (root / "platforms" / "android-36" / "android.jar").touch()
            (root / "build-tools" / "36.0.0").mkdir(parents=True)
            (root / "build-tools" / "36.0.0" / "aapt2").touch()
            (root / "platform-tools").mkdir(parents=True)
            (root / "platform-tools" / "adb").touch()
            checks = gate.check_sdk(root)
            self.assertTrue(all(check.ok for check in checks))

    def test_qt_target_and_host_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            version_root = Path(tmp) / "6.11.1"
            target = version_root / "android_arm64_v8a"
            host = version_root / "gcc_64"
            (target / "bin").mkdir(parents=True)
            (target / "bin" / "qt-cmake").touch()
            (host / "bin").mkdir(parents=True)
            (host / "bin" / "qmlimportscanner").touch()
            checks = gate.check_qt(target, host)
            self.assertTrue(all(check.ok for check in checks))


if __name__ == "__main__":
    unittest.main()

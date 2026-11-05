from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LaunchQmlManifestContract(unittest.TestCase):
    def test_source_launcher_refreshes_qml_manifest_after_successful_native_build(self):
        cmake = (ROOT / "native" / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (ROOT / "launch.bat").read_text(encoding="utf-8")

        match = re.search(
            r"add_custom_target\(colosseum_runtime_ready(?P<body>.*?)\n\)",
            cmake,
            re.DOTALL,
        )
        self.assertIsNotNone(
            match,
            "source launch needs an always-run runtime-ready target, not only a POST_BUILD hook",
        )
        body = match.group("body")
        self.assertIn("DEPENDS colosseum", body)
        self.assertIn("write_qml_build_manifest.cmake", body)
        self.assertIn('--target colosseum_runtime_ready', launcher)

    def test_default_launch_builds_and_explicit_fast_launch_checks_qml(self):
        launcher = (ROOT / "launch.bat").read_text(encoding="utf-8")
        manifest = (ROOT / "native" / "bootstrap" / "write_qml_build_manifest.cmake").read_text(encoding="utf-8")
        self.assertIn('if /I "%~1"=="--fast" set "FAST_REQUESTED=1"', launcher)
        self.assertIn('if defined FAST_REQUESTED goto :fast_launch', launcher)
        self.assertIn('if defined FAST_REQUESTED goto :finish', launcher)
        self.assertIn('-DVERIFY_ONLY=ON', launcher)
        self.assertIn('if(VERIFY_ONLY)', manifest)
        self.assertIn('file(READ "${OUTPUT_FILE}" built_manifest_contents)', manifest)
        self.assertIn('if(NOT built_manifest_contents STREQUAL manifest_contents)', manifest)

    def test_launcher_surfaces_qt_startup_diagnostics(self):
        launcher = (ROOT / "launch.bat").read_text(encoding="utf-8")
        self.assertIn('set "QT_FORCE_STDERR_LOGGING=1"', launcher)
        self.assertIn('%APPDATA%\\Brotherhood\\Colosseum\\logs\\colosseum.log', launcher)


if __name__ == "__main__":
    unittest.main()

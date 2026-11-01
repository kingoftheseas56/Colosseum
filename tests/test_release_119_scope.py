import pathlib
import os
import shutil
import tempfile
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Release119Scope(unittest.TestCase):
    def test_feria_is_present_but_gated_and_lazily_constructed(self):
        self.assertTrue((ROOT / "native/feria").is_dir())
        self.assertTrue((ROOT / "qml/feria").is_dir())

        main_cpp = (ROOT / "native/main.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "feria/PorticoComposition.h"', main_cpp)
        self.assertIn("std::unique_ptr<PorticoComposition> feriaDiscovery;", main_cpp)
        self.assertNotIn("\n    PorticoComposition feriaDiscovery;", main_cpp)
        self.assertIn("auto ensureFeriaRuntime =", main_cpp)

        main_qml = (ROOT / "qml/Main.qml").read_text(encoding="utf-8")
        self.assertIn("readonly property bool feriaComingSoon: true", main_qml)
        self.assertIn("function ensureFeriaRuntime()", main_qml)
        self.assertIn('if ((requestedWorld === "feria" && win.feriaComingSoon !== false) || (requestedWorld === "vinyl" && !win.vinylEnabled)) return', main_qml)
        self.assertIn("if (!win.ensureFeriaRuntime()) return", main_qml)

        self.assertIn("feriaEnabled: win.feriaAvailable && !win.feriaComingSoon", main_qml)
        center = (ROOT / "qml/account/AccountCenter.qml").read_text(encoding="utf-8")
        self.assertIn("property bool feriaEnabled: false", center)
        self.assertIn('Qt.platform.os !== "android" && feriaEnabled', center)

        topbar_qml = (ROOT / "qml/TopBar.qml").read_text(encoding="utf-8")
        self.assertIn('Pill { label: "Feria"; comingSoon: true }', topbar_qml)
        self.assertIn('comingSoon: !qualificationEnabled', topbar_qml)
        self.assertIn('Colosseum::vinylLiveEnabled()', main_cpp)

    def test_packager_rejects_qualification_and_fixture_builds(self):
        git_bash = pathlib.Path("C:/Program Files/Git/bin/bash.exe")
        bash = str(git_bash) if git_bash.is_file() else shutil.which("bash")
        if not bash:
            self.skipTest("Bash is required to execute the real packaging guard")
        script = (ROOT / "scripts/installer/package_release.sh").read_text(encoding="utf-8")
        start = script.index("# Qualification builds must never")
        end = script.index("# API builds use semantic queries", start)
        guard = script[start:end]
        with tempfile.TemporaryDirectory(prefix="colosseum-release-guard-") as directory:
            cache = pathlib.Path(directory) / "CMakeCache.txt"
            for option in (None, "COLOSSEUM_ENABLE_VINYL_QUALIFICATION", "COLOSSEUM_ENABLE_VINYL_TEST_FIXTURES"):
                with self.subTest(option=option):
                    cache.write_text("CMAKE_BUILD_TYPE:STRING=Release\n" +
                                     (f"{option}:BOOL=ON\n" if option else
                                      "COLOSSEUM_ENABLE_VINYL_QUALIFICATION:BOOL=OFF\nCOLOSSEUM_ENABLE_VINYL_TEST_FIXTURES:BOOL=OFF\n"))
                    result = subprocess.run([bash, "--noprofile", "--norc", "-c", guard + "echo ACCEPTED"],
                                            cwd=directory, env=dict(os.environ, BUILD_DIR="."),
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 1 if option else 0, result.stdout + result.stderr)
                    if option:
                        self.assertIn(f"refusing to package {option}=ON build", result.stdout)
                        self.assertNotIn("ACCEPTED", result.stdout)
                    else:
                        self.assertIn("ACCEPTED", result.stdout)

    def test_version_and_first_party_discovery(self):
        self.assertIn("VERSION 1.1.10", (ROOT / "native/CMakeLists.txt").read_text(encoding="utf-8"))
        main = (ROOT / "native/main.cpp").read_text(encoding="utf-8")
        self.assertIn("https://getcolosseum.world/updates/stable.json", main)
        self.assertNotIn("https://api.github.com/repos/kingoftheseas56/Colosseum/releases/latest", main)
        self.assertIn('Pill { label: "Universes" }', (ROOT / "qml/TopBar.qml").read_text(encoding="utf-8"))

    def test_packager_requires_the_qualified_api_and_standard_edition(self):
        git_bash = pathlib.Path("C:/Program Files/Git/bin/bash.exe")
        bash = str(git_bash) if git_bash.is_file() else shutil.which("bash")
        if not bash:
            self.skipTest("Bash is required to execute the real packaging guard")
        script = (ROOT / "scripts/installer/package_release.sh").read_text(encoding="utf-8")
        guard = script[script.index("# Qualification builds must never"):script.index("# API builds use semantic queries")]
        production = "https://colosseum-content-api.colosseum-watchparty-relay.workers.dev"
        with tempfile.TemporaryDirectory(prefix="colosseum-api-release-guard-") as directory:
            cache = pathlib.Path(directory) / "CMakeCache.txt"
            for origin, no_account, accepted in [(production, False, True), ("", False, False),
                    (production.replace("content-api.", "content-api-staging."), False, False),
                    (production, True, False)]:
                with self.subTest(origin=origin, no_account=no_account):
                    cache.write_text("CMAKE_BUILD_TYPE:STRING=Release\n"
                                     f"COLOSSEUM_CONTENT_API_URL:STRING={origin}\n"
                                     f"COLOSSEUM_NO_ACCOUNT_EDITION:BOOL={'ON' if no_account else 'OFF'}\n")
                    result = subprocess.run([bash, "--noprofile", "--norc", "-c", guard + "echo ACCEPTED"],
                                            cwd=directory, env=dict(os.environ, BUILD_DIR=".", VERSION="1.1.10"),
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0 if accepted else 1, result.stdout + result.stderr)
                    self.assertEqual("ACCEPTED" in result.stdout, accepted)

    def test_production_signing_trust_is_unchanged(self):
        baseline = subprocess.check_output(["git", "show", "v1.1.8:native/update/UpdatePublicKey.h"], cwd=ROOT)
        self.assertEqual(baseline.replace(b"\r\n", b"\n"), (ROOT / "native/update/UpdatePublicKey.h").read_bytes().replace(b"\r\n", b"\n"))


if __name__ == "__main__":
    unittest.main()

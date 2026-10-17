import pathlib
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
        self.assertIn('if ((requestedWorld === "feria" && win.feriaComingSoon !== false) || requestedWorld === "vinyl") return', main_qml)
        self.assertIn("if (!win.ensureFeriaRuntime()) return", main_qml)

        self.assertIn("feriaEnabled: win.feriaAvailable && !win.feriaComingSoon", main_qml)
        center = (ROOT / "qml/account/AccountCenter.qml").read_text(encoding="utf-8")
        self.assertIn("property bool feriaEnabled: false", center)
        self.assertIn('Qt.platform.os !== "android" && feriaEnabled', center)

        topbar_qml = (ROOT / "qml/TopBar.qml").read_text(encoding="utf-8")
        self.assertIn('Pill { label: "Feria"; comingSoon: true }', topbar_qml)
        self.assertIn('Pill { label: "Vinyl"; comingSoon: true }', topbar_qml)

    def test_version_and_first_party_discovery(self):
        self.assertIn("VERSION 1.1.9", (ROOT / "native/CMakeLists.txt").read_text(encoding="utf-8"))
        main = (ROOT / "native/main.cpp").read_text(encoding="utf-8")
        self.assertIn("https://getcolosseum.world/updates/stable.json", main)
        self.assertNotIn("https://api.github.com/repos/kingoftheseas56/Colosseum/releases/latest", main)
        self.assertIn('Pill { label: "Universes" }', (ROOT / "qml/TopBar.qml").read_text(encoding="utf-8"))

    def test_production_signing_trust_is_unchanged(self):
        baseline = subprocess.check_output(["git", "show", "v1.1.8:native/update/UpdatePublicKey.h"], cwd=ROOT)
        self.assertEqual(baseline.replace(b"\r\n", b"\n"), (ROOT / "native/update/UpdatePublicKey.h").read_bytes().replace(b"\r\n", b"\n"))


if __name__ == "__main__":
    unittest.main()

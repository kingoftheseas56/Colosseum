import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Release119Scope(unittest.TestCase):
    def test_no_feria_source_or_shell_entry_is_shipped(self):
        self.assertFalse((ROOT / "native/feria").exists())
        self.assertFalse((ROOT / "qml/feria").exists())
        for relative in ("native/main.cpp", "native/CMakeLists.txt", "qml/Main.qml", "qml/TopBar.qml"):
            self.assertNotIn("feria", (ROOT / relative).read_text(encoding="utf-8").lower())

    def test_version_and_first_party_discovery(self):
        self.assertIn("VERSION 1.1.9", (ROOT / "native/CMakeLists.txt").read_text())
        main = (ROOT / "native/main.cpp").read_text(encoding="utf-8")
        self.assertIn("https://getcolosseum.world/updates/stable.json", main)
        self.assertNotIn("https://api.github.com/repos/kingoftheseas56/Colosseum/releases/latest", main)
        self.assertIn('Pill { label: "Universes" }', (ROOT / "qml/TopBar.qml").read_text(encoding="utf-8"))

    def test_production_signing_trust_is_unchanged(self):
        baseline = subprocess.check_output(["git", "show", "v1.1.8:native/update/UpdatePublicKey.h"], cwd=ROOT)
        self.assertEqual(baseline.replace(b"\r\n", b"\n"), (ROOT / "native/update/UpdatePublicKey.h").read_bytes().replace(b"\r\n", b"\n"))


if __name__ == "__main__":
    unittest.main()

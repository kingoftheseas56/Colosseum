#!/usr/bin/env python3
"""PF-09: the Android manifest names adaptive launcher artwork (icon + roundIcon) instead of
Qt's INSERT_APP_ICON placeholder, every referenced resource exists in the package source
copied by native/cmake/android.cmake, and the adaptive icon resolves its background color and
foreground vector. API 28 floor => mipmap-anydpi-v26 always applies. Package id/signer are
not part of this check (see PF-10)."""
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "native/platform/android"
NS = "{http://schemas.android.com/apk/res/android}"


def resource_file(ref):
    kind, name = ref.lstrip("@").split("/", 1)
    if kind == "mipmap":
        return ANDROID / "res/mipmap-anydpi-v26" / f"{name}.xml"
    if kind == "drawable":
        return ANDROID / "res/drawable" / f"{name}.xml"
    raise AssertionError(f"unexpected resource kind {ref}")


class AndroidLauncherIcon(unittest.TestCase):
    def test_manifest_references_adaptive_icons(self):
        app = ET.parse(ANDROID / "AndroidManifest.xml").getroot().find("application")
        self.assertEqual(app.get(NS + "icon"), "@mipmap/ic_launcher")
        self.assertEqual(app.get(NS + "roundIcon"), "@mipmap/ic_launcher_round")
        self.assertNotIn("INSERT_APP_ICON", (ANDROID / "AndroidManifest.xml").read_text(encoding="utf-8"))

    def test_adaptive_icons_resolve(self):
        colors = {c.get("name"): c.text for c in
                  ET.parse(ANDROID / "res/values/ic_launcher_background.xml").getroot().findall("color")}
        for ref in ("@mipmap/ic_launcher", "@mipmap/ic_launcher_round"):
            path = resource_file(ref)
            self.assertTrue(path.is_file(), path)
            icon = ET.parse(path).getroot()
            self.assertEqual(icon.tag, "adaptive-icon")
            background = icon.find("background").get(NS + "drawable")
            foreground = icon.find("foreground").get(NS + "drawable")
            self.assertTrue(background.startswith("@color/"))
            self.assertRegex(colors.get(background.split("/", 1)[1], ""), r"^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$")
            vector = ET.parse(resource_file(foreground)).getroot()
            self.assertEqual(vector.tag, "vector")
            self.assertEqual(vector.get(NS + "viewportWidth"), "108")
            self.assertEqual(vector.get(NS + "width"), "108dp")

    def test_resources_are_inside_the_copied_package_source(self):
        # android.cmake copies every file under native/platform/android except the bootstrap
        # sources and the desktop vault bridge.
        for rel in ("res/drawable/ic_launcher_foreground.xml", "res/mipmap-anydpi-v26/ic_launcher.xml",
                    "res/mipmap-anydpi-v26/ic_launcher_round.xml", "res/values/ic_launcher_background.xml"):
            self.assertTrue((ANDROID / rel).is_file(), rel)
            self.assertFalse(rel.startswith("src/org/colosseum/vault/"))


if __name__ == "__main__":
    unittest.main()

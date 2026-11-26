"""Android image overrides stay in step with the art they replace.

packaging/android/asset-overrides/ holds same-named WebP copies of large PNGs
for the APK. Each must be real WebP, smaller than its source, and made from the
current source (the manifest pins the source SHA-256). Regenerate with
scripts/android/make_android_image_overrides.py when the art changes.
"""
import hashlib
import json
import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
ROOT = REPO / "packaging" / "android" / "asset-overrides"


class AndroidImageOverrides(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))["files"]

    def test_every_override_is_current_webp(self):
        self.assertTrue(self.manifest)
        for rel, entry in self.manifest.items():
            with self.subTest(rel=rel):
                source = (REPO / rel).read_bytes()
                override = (ROOT / rel).read_bytes()
                self.assertEqual(hashlib.sha256(source).hexdigest(), entry["source_sha256"],
                                 "source art changed: regenerate the Android override")
                self.assertEqual(hashlib.sha256(override).hexdigest(), entry["override_sha256"])
                self.assertEqual(override[:4], b"RIFF")
                self.assertEqual(override[8:12], b"WEBP")
                self.assertLess(len(override), len(source))

    def test_no_orphan_overrides(self):
        files = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*") if p.is_file()}
        files.discard("manifest.json")
        self.assertEqual(files, set(self.manifest))

    def test_android_staging_uses_overrides(self):
        cmake = (REPO / "native" / "cmake" / "android.cmake").read_text(encoding="utf-8")
        self.assertIn("packaging/android/asset-overrides/", cmake)


if __name__ == "__main__":
    unittest.main()

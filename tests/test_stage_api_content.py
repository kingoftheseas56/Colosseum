import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("stage_api_content", Path(__file__).resolve().parents[1] / "scripts/stage_api_content.py")
staging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(staging)


class ApiStagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / "package"
        for name in ["qml/Main.qml", "native/build-msvc/colosseum.exe", "assets/universes/art.png", "assets/undeclared.png", "data/user.sqlite"]:
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        for name in ["mal", "imdb", "comics", "tankoban"]:
            (self.stage / "data" / (name + "_catalog.db")).write_bytes(b"catalogue")
        self.bundle = {"manifest": {"documents": {str(i): {} for i in range(10)}, "assets": {
            "assets/universes/art.png": {"sha256": hashlib.sha256(b"assets/universes/art.png").hexdigest()}
        }}}
        self.save()

    def save(self):
        starter = self.stage / "assets/content-starter/bundle.json"
        starter.parent.mkdir(parents=True, exist_ok=True)
        starter.write_text(json.dumps(self.bundle), encoding="utf-8")

    def test_optimized_delivery_verifies_original_bytes_before_removal(self):
        asset = self.bundle["manifest"]["assets"]["assets/universes/art.png"]
        asset["sourceSha256"] = asset["sha256"]
        asset["sha256"] = hashlib.sha256(b"optimized-webp-delivery").hexdigest()
        self.save()
        self.assertEqual(staging.stage_api_content(self.stage)["files"], 5)
        (self.stage / "assets/universes/art.png").write_bytes(b"changed-original")
        with self.assertRaises(ValueError):
            staging.stage_api_content(self.stage, apply=True)
        self.assertTrue((self.stage / "data/mal_catalog.db").exists())

    def test_dry_run_preserves_every_file_then_apply_removes_only_declared_delivery(self):
        before = {p.relative_to(self.stage): p.read_bytes() for p in self.stage.rglob("*") if p.is_file()}
        preview = staging.stage_api_content(self.stage)
        self.assertEqual(preview["files"], 5)
        self.assertFalse(preview["applied"])
        self.assertEqual(before, {p.relative_to(self.stage): p.read_bytes() for p in self.stage.rglob("*") if p.is_file()})
        self.assertTrue(staging.stage_api_content(self.stage, apply=True)["applied"])
        for name in ["assets/undeclared.png", "data/user.sqlite", "assets/content-starter/bundle.json", "qml/Main.qml", "native/build-msvc/colosseum.exe"]:
            self.assertEqual((self.stage / name).read_bytes(), before[Path(name)])
        self.assertFalse((self.stage / "assets/universes/art.png").exists())
        self.assertEqual(list((self.stage / "data").glob("*_catalog.db")), [])

    def test_hash_mismatch_aborts_without_removing_other_targets(self):
        (self.stage / "assets/universes/art.png").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "differs"):
            staging.stage_api_content(self.stage, apply=True)
        self.assertEqual(len(list((self.stage / "data").glob("*_catalog.db"))), 4)

    def test_outside_asset_and_json_targets_are_rejected(self):
        for relative in ["../private.png", "assets/../../private.png", "assets/content-starter/bundle.json"]:
            self.bundle["manifest"]["assets"] = {relative: {"sha256": "a" * 64}}
            self.save()
            with self.assertRaisesRegex(ValueError, "outside artwork"):
                staging.stage_api_content(self.stage, apply=True)
        self.assertTrue((self.stage / "assets/universes/art.png").is_file())

    def test_checkout_and_incomplete_package_are_rejected(self):
        (self.stage / ".git").write_text("gitdir: elsewhere")
        with self.assertRaisesRegex(ValueError, "Git checkout"):
            staging.stage_api_content(self.stage, apply=True)
        (self.stage / ".git").unlink()
        (self.stage / "native/build-msvc/colosseum.exe").unlink()
        with self.assertRaisesRegex(ValueError, "runtime missing"):
            staging.stage_api_content(self.stage, apply=True)

    def test_incomplete_metadata_starter_is_rejected(self):
        self.bundle["manifest"]["documents"] = {}
        self.save()
        with self.assertRaisesRegex(ValueError, "starter missing"):
            staging.stage_api_content(self.stage, apply=True)


if __name__ == "__main__":
    unittest.main()

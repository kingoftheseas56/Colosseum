import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("content_publisher", Path(__file__).resolve().parents[1] / "projection/publish_content.py")
publisher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(publisher)


class ContentPublisherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.native = self.root / "native-source"
        (self.native / "qml").mkdir(parents=True)
        (self.native / "assets/universes").mkdir(parents=True)
        self.documents = {"universes-roster": {"wallpaper": "../assets/wallpaper/sagas/story.jpg"}}
        wallpaper = self.native / "assets/wallpaper/sagas/story.jpg"
        wallpaper.parent.mkdir(parents=True)
        wallpaper.write_bytes(b"wallpaper")
        (self.native / "assets/universes/a.png").write_bytes(b"same art")
        (self.native / "assets/universes/b.png").write_bytes(b"same art")
        (self.native / "assets/universes/source.json").write_text("{}")

    def export(self, command, **kwargs):
        destination = Path(command[-1])
        for name, data in self.documents.items():
            revision = hashlib.sha256(json.dumps(data).encode()).hexdigest()
            (destination / (name + ".json")).write_text(json.dumps({"schemaVersion": 1, "revision": revision, "data": data}), encoding="utf-8")

    def build(self):
        with patch.object(publisher.subprocess, "run", side_effect=self.export):
            return publisher.build(self.native, self.root / "output", self.root / "starter/bundle.json")

    def test_declared_wallpaper_deduplicated_art_and_metadata_starter_preserve_sources(self):
        before = {p.relative_to(self.native): p.read_bytes() for p in self.native.rglob("*") if p.is_file()}
        result = self.build()
        bundle = json.loads((self.root / "starter/bundle.json").read_text())
        served = json.loads((self.root / "output/internal/content/rpc-bundle.json").read_text())
        self.assertEqual(bundle, served["result"])
        assets = bundle["manifest"]["assets"]
        self.assertEqual(result["artworkMappings"], 3)
        self.assertIn("assets/wallpaper/sagas/story.jpg", assets)
        self.assertEqual(assets["assets/universes/a.png"]["path"], assets["assets/universes/b.png"]["path"])
        self.assertNotIn("assets/universes/source.json", assets)
        self.assertEqual(len(list((self.root / "output/assets/content").iterdir())), 2)
        for description in assets.values():
            raw = (self.root / "output" / description["path"].lstrip("/")).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), description["sha256"])
        raw = bundle["documents"]["universes-roster"].encode()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), bundle["manifest"]["documents"]["universes-roster"]["sha256"])
        self.assertEqual(before, {p.relative_to(self.native): p.read_bytes() for p in self.native.rglob("*") if p.is_file()})

    def test_missing_declared_art_is_rejected(self):
        self.documents["universes-roster"]["wallpaper"] = "../assets/missing.jpg"
        with self.assertRaisesRegex(ValueError, "missing or outside"):
            self.build()

    def test_declared_traversal_is_rejected(self):
        (self.native / "private.png").write_bytes(b"private")
        self.documents["universes-roster"]["wallpaper"] = "../assets/../private.png"
        with self.assertRaisesRegex(ValueError, "missing or outside"):
            self.build()
        self.assertEqual((self.native / "private.png").read_bytes(), b"private")

    def test_free_static_asset_size_cap_is_enforced(self):
        with (self.native / "assets/universes/oversized.png").open("wb") as stream:
            stream.truncate(25 * 1024 * 1024 + 1)
        with self.assertRaisesRegex(ValueError, "free static asset limit"):
            self.build()

    def test_existing_output_is_never_overwritten(self):
        (self.root / "output").mkdir()
        marker = self.root / "output/keep"
        marker.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "fresh output"):
            self.build()
        self.assertEqual(marker.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()

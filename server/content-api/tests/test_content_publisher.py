import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

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
        Image.new("RGB", (64, 32), "blue").save(wallpaper)
        for name in ["a.png", "b.png"]:
            Image.new("RGB", (32, 24), "red").save(self.native / "assets/universes" / name)
        (self.native / "assets/universes/source.json").write_text("{}")

    def export(self, command, **kwargs):
        destination = Path(command[-1])
        for name, data in self.documents.items():
            revision = hashlib.sha256(json.dumps(data).encode()).hexdigest()
            (destination / (name + ".json")).write_text(json.dumps({"schemaVersion": 1, "revision": revision, "data": data}), encoding="utf-8")

    def build(self, previous=None):
        with patch.object(publisher.subprocess, "run", side_effect=self.export):
            return publisher.build(self.native, self.root / "output", self.root / "starter/bundle.json", previous)

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
        for relative, description in assets.items():
            raw = (self.root / "output" / description["path"].lstrip("/")).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), description["sha256"])
            self.assertEqual(description["sourceSha256"], hashlib.sha256((self.native / relative).read_bytes()).hexdigest())
        raw = bundle["documents"]["universes-roster"].encode()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), bundle["manifest"]["documents"]["universes-roster"]["sha256"])
        self.assertEqual(before, {p.relative_to(self.native): p.read_bytes() for p in self.native.rglob("*") if p.is_file()})

    def test_missing_declared_art_is_rejected(self):
        self.documents["universes-roster"]["wallpaper"] = "../assets/missing.jpg"
        with self.assertRaisesRegex(ValueError, "missing or outside"):
            self.build()

    def test_native_neutral_backgrounds_are_published_without_uploading_other_local_art(self):
        backgrounds = ["captured-motion.jpg", "cold-ripple.jpg"]
        for name in backgrounds:
            Image.new("RGB", (40, 24), "green").save(self.native / "assets/wallpaper" / name)
        private = self.native / "assets/wallpaper/unrelated-local.jpg"
        Image.new("RGB", (40, 24), "yellow").save(private)
        self.build()
        bundle = json.loads((self.root / "starter/bundle.json").read_text())
        assets = bundle["manifest"]["assets"]
        for name in backgrounds:
            relative = "assets/wallpaper/" + name
            self.assertIn(relative, assets)
            asset = assets[relative]
            original = (self.native / relative).read_bytes()
            self.assertEqual(asset["sourceSha256"], hashlib.sha256(original).hexdigest())
            delivered = (self.root / "output" / asset["path"].lstrip("/")).read_bytes()
            self.assertEqual(hashlib.sha256(delivered).hexdigest(), asset["sha256"])
        self.assertNotIn("assets/wallpaper/unrelated-local.jpg", assets)

    def test_declared_traversal_is_rejected(self):
        (self.native / "private.png").write_bytes(b"private")
        self.documents["universes-roster"]["wallpaper"] = "../assets/../private.png"
        with self.assertRaisesRegex(ValueError, "missing or outside"):
            self.build()
        self.assertEqual((self.native / "private.png").read_bytes(), b"private")

    def test_atlas_and_transparent_badges_preserve_pixels_and_dimensions(self):
        for name, mode, color in [("voyage-atlas.png", "RGB", "green"), ("badge.png", "RGBA", (255, 0, 0, 64))]:
            source = self.native / "assets/universes" / name
            Image.new(mode, (2048, 24), color).save(source)
        self.build()
        assets = json.loads((self.root / "starter/bundle.json").read_text())["manifest"]["assets"]
        for name in ["voyage-atlas.png", "badge.png"]:
            asset = assets["assets/universes/" + name]
            self.assertTrue(asset["lossless"])
            with Image.open(self.native / "assets/universes" / name) as original, Image.open(self.root / "output" / asset["path"].lstrip("/")) as delivered:
                self.assertEqual(delivered.size, original.size)
                self.assertEqual(delivered.convert("RGBA").tobytes(), original.convert("RGBA").tobytes())

    def test_large_hero_delivery_is_bounded_and_original_remains_unchanged(self):
        source = self.native / "assets/universes/hero.png"
        Image.new("RGB", (2400, 1600), "purple").save(source)
        original = source.read_bytes()
        self.build()
        asset = json.loads((self.root / "starter/bundle.json").read_text())["manifest"]["assets"]["assets/universes/hero.png"]
        self.assertEqual((asset["width"], asset["height"]), (1920, 1280))
        self.assertFalse(asset["lossless"])
        self.assertEqual(source.read_bytes(), original)
        self.assertEqual(asset["sourceSha256"], hashlib.sha256(original).hexdigest())

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

    def test_previous_immutable_assets_are_retained_for_running_clients(self):
        previous = self.root / "previous"
        raw = b"prior delivered art"
        old = previous / "assets/content" / (hashlib.sha256(raw).hexdigest() + ".png")
        old.parent.mkdir(parents=True)
        old.write_bytes(raw)
        result = self.build(previous)
        self.assertEqual(result["retainedArtworkFiles"], 1)
        self.assertEqual((self.root / "output/assets/content" / old.name).read_bytes(), raw)
        self.assertEqual(old.read_bytes(), raw)

    def test_corrupted_previous_asset_is_rejected(self):
        previous = self.root / "previous"
        old = previous / "assets/content" / ("a" * 64 + ".png")
        old.parent.mkdir(parents=True)
        old.write_bytes(b"wrong checksum")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.build(previous)


if __name__ == "__main__":
    unittest.main()

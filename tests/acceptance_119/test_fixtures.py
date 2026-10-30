"""Focused fixture integrity tests. These do not qualify assembled-app behavior."""
import importlib.util
import json
import os
import stat
from pathlib import Path
import shutil
import re
import struct
import zlib
import subprocess
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("acceptance_fixtures", HERE / "fixtures.py")


class FixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (HERE / "fixtures.py").is_file():
            return
        cls.fixtures = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(cls.fixtures)
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name) / "portable fixture root"
        cls.manifest = cls.fixtures.generate(cls.root)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def require_generator(self):
        self.assertTrue((HERE / "fixtures.py").is_file(), "Fixture generator is not implemented")

    def test_numbered_cbz_has_real_distinct_pages_and_stable_bytes(self):
        self.require_generator()
        a = self.root / "comics/a/twin.cbz"
        b = self.root / "comics/b/twin.cbz"
        with zipfile.ZipFile(a) as archive:
            self.assertEqual(archive.namelist(), [f"page-{i:02}.png" for i in range(1, 9)])
            pages = [archive.read(name) for name in archive.namelist()]
            self.assertTrue(all(page.startswith(b"\x89PNG\r\n\x1a\n") for page in pages))
            self.assertEqual(len(set(pages)), 8)
        self.assertNotEqual(a.read_bytes(), b.read_bytes())
        old = a.read_bytes()
        self.fixtures.write_cbz(a, "A")
        self.assertEqual(old, a.read_bytes())

    def test_pdf_is_searchable_with_exact_page_term_counts(self):
        self.require_generator()
        self.assertIsNotNone(shutil.which("pdftotext"), "pdftotext required to validate searchable PDF")
        path = self.root / "books/search.pdf"
        result = subprocess.run(["pdftotext", "-layout", str(path), "-"], check=True, capture_output=True, text=True)
        pages = result.stdout.split("\f")[:4]
        self.assertEqual(len(pages), 4)
        self.assertEqual([p.count("ORBIT") for p in pages], [1, 1, 0, 1])
        self.assertIn("NEPTUNE", pages[2])
        self.assertEqual(sum(p.count("NEPTUNE") for p in pages), 1)
        old = path.read_bytes()
        self.fixtures.write_pdf(path)
        self.assertEqual(old, path.read_bytes())

    def test_media_decodes_and_duration_supports_interaction(self):
        self.require_generator()
        self.assertIsNotNone(shutil.which("ffprobe"), "ffprobe required to validate playable fixtures")
        clip = self.root / "media/reference.mp4"
        result = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(clip)], check=True, capture_output=True, text=True)
        probe = json.loads(result.stdout)
        video = next(stream for stream in probe["streams"] if stream["codec_type"] == "video")
        self.assertEqual((video["width"], video["height"]), (160, 90))
        self.assertGreaterEqual(float(probe["format"]["duration"]), 60)
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(clip), "-frames:v", "1", "-f", "null", "-"], check=True, capture_output=True)

    def test_vault_shapes_and_unavailable_root(self):
        self.require_generator()
        expected = {"empty": 0, "film": 1, "sibling": 4, "nested": 3, "anime": 2, "clips": 2}
        for shape, count in expected.items():
            self.assertEqual(len(list((self.root / "vault" / shape).rglob("*.mp4"))), count, shape)
            seed = json.loads((self.root / "seeds" / f"vault-{shape}" / "vault/config.json").read_text())
            self.assertEqual(len(seed["roots"]), 1)
            self.assertTrue(seed["roots"][0]["confirmed"])
        self.assertFalse((self.root / "vault/unavailable").exists())

    def test_seed_identity_matches_actual_file_stat_and_same_name_is_distinct(self):
        self.require_generator()
        recent = json.loads((self.root / "seeds/comic/vault/open-recent.json").read_text())["items"]
        self.assertEqual(len(recent), 2)
        self.assertNotEqual(recent[0]["vaultId"], recent[1]["vaultId"])
        for item in recent:
            self.assertEqual(item["vaultId"], self.fixtures.vault_id(Path(item["path"])))
        self.assertEqual(self.manifest["tokens"]["COMIC_A_ID"], recent[0]["vaultId"])

    def test_refuses_unowned_nonempty_root(self):
        self.require_generator()
        other = self.root.parent / "unowned"
        other.mkdir()
        (other / "keep.txt").write_text("untouched")
        with self.assertRaises(ValueError):
            self.fixtures.generate(other)
        self.assertEqual((other / "keep.txt").read_text(), "untouched")

    def test_missing_and_refresh_mutations_are_owned_and_reversible(self):
        self.require_generator()
        self.fixtures.apply_mutation(self.root, "comic-a-missing")
        self.assertFalse((self.root / "comics/a/twin.cbz").exists())
        self.fixtures.apply_mutation(self.root, "comic-a-restore")
        self.assertTrue((self.root / "comics/a/twin.cbz").exists())
        self.fixtures.apply_mutation(self.root, "vault-add-clip")
        self.assertEqual(len(list((self.root / "vault/clips/loose").glob("*.mp4"))), 3)
        self.fixtures.apply_mutation(self.root, "vault-remove-added-clip")
        self.assertEqual(len(list((self.root / "vault/clips/loose").glob("*.mp4"))), 2)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "../../outside")


    def test_all_cbz_images_decode_complete_rgb_scanlines(self):
        self.require_generator()
        with zipfile.ZipFile(self.root / "comics/a/twin.cbz") as archive:
            for name in archive.namelist():
                data = archive.read(name)
                offset = 8
                compressed = bytearray()
                while offset < len(data):
                    length = struct.unpack(">I", data[offset:offset+4])[0]
                    kind = data[offset+4:offset+8]
                    body = data[offset+8:offset+8+length]
                    crc = struct.unpack(">I", data[offset+8+length:offset+12+length])[0]
                    self.assertEqual(crc, zlib.crc32(kind+body) & 0xffffffff)
                    if kind == b"IDAT": compressed.extend(body)
                    offset += length + 12
                raw = zlib.decompress(compressed)
                self.assertEqual(len(raw), 640*(480*3+1))
                self.assertTrue(all(raw[y*(480*3+1)] == 0 for y in range(640)))

    def test_reader_vault_scenario_native_schema_and_reply_checks(self):
        self.require_generator()
        root = HERE.parents[1]
        scenario_root = root / "tests/lanista_scenarios/acceptance_119"
        source = (root / "native/devtools/LanistaServer.cpp").read_text()
        commands = set(re.findall(r'add(?:Read|Drive)\(QStringLiteral\("([^"\n]+)"\)', source))
        files = sorted(scenario_root.glob("reader_*.json")) + sorted(scenario_root.glob("vault_*.json"))
        self.assertGreaterEqual(len(files), 20)
        for path in files:
            scenario = json.loads(path.read_text())
            self.assertTrue(scenario["steps"], path.name)
            labels = [step["label"] for step in scenario["steps"]]
            self.assertEqual(len(labels), len(set(labels)), path.name)
            for index, step in enumerate(scenario["steps"]):
                label = f"{path.name} step {index}"
                if "presentation_wait_ms" in step:
                    self.assertEqual(step["presentation_wait_ms"], 350, label)
                    self.assertEqual(scenario["steps"][index - 1]["payload"].get("object"), "player", label)
                    self.assertEqual(scenario["steps"][index + 1]["payload"].get("grab", {}).get("target"), "player", label)
                    continue
                self.assertIn(step["cmd"], commands, label)
                self.assertIn({"path": "type", "op": "==", "value": "reply"}, step["expect"], label)
                if step["cmd"] == "ui-wait-for":
                    self.assertLessEqual(step["payload"]["timeout_ms"], 60000, label)
                raw = json.dumps(step)
                for token in re.findall(r'\$\{([A-Z_]+)\}', raw):
                    self.assertIn(token, self.manifest["tokens"], label)
                self.assertNotRegex(raw, r"[A-Z]:[/\\]Users[/\\]|/home/|/workspace/", label)

    def test_pdf_visible_search_selectors_are_named(self):
        root = HERE.parents[1]
        sheet = (root / "qml/reader2/SearchSheet.qml").read_text()
        bar = (root / "qml/reader2/TopBar.qml").read_text()
        self.assertIn('objectName: "reader2SearchCount"', sheet)
        self.assertIn('objectName: "reader2SearchEmpty"', sheet)
        self.assertIn('objectName: "reader2SearchAction"', bar)
        comic = (root / "qml/comicreader/ComicReaderCommandBar.qml").read_text()
        self.assertIn('objectName: "readerCommand_" + cmd.modelData', comic)
        vault = (root / "qml/VaultPage.qml").read_text()
        self.assertIn('objectName: "vaultFolderList"', vault)
        self.assertIn('objectName: "vaultFolderTile_" + folderTile.modelData.key', vault)
        recent = (root / "qml/OpenRecentPanel.qml").read_text()
        self.assertIn('objectName: "openRecentAvailability_" + index', recent)



class ReaderJourneySynchronizationTests(unittest.TestCase):
    scenarios = HERE.parents[1] / "tests/lanista_scenarios/acceptance_119"

    def steps(self, name):
        return json.loads((self.scenarios / f"{name}.json").read_text())["steps"]

    def test_continue_actions_wait_for_lazy_loader_before_query(self):
        for name in ("reader_comic_restart", "reader_comic_same_name_restart",
                     "reader_comic_missing", "reader_pdf_restart"):
            steps = self.steps(name)
            for index, step in enumerate(steps):
                target = step.get("payload", {}).get("object", step.get("payload", {}).get("target", ""))
                if step.get("cmd") in ("ui-query", "ui-click") and target.startswith("continueResume_"):
                    self.assertTrue(any(prior["cmd"] == "ui-wait-for"
                                        and prior["payload"].get("object") == target
                                        and prior["payload"].get("prop") == "visible"
                                        and prior["payload"].get("value") is True
                                        for prior in steps[:index]), name)

    def test_nested_season_clicks_follow_real_scroll(self):
        steps = self.steps("vault_nested_prepare")
        for index, step in enumerate(steps):
            if (step["cmd"] == "ui-query"
                    and step["payload"]["object"].endswith("/season 1_hitArea")):
                self.assertEqual(steps[index - 2]["cmd"], "ui-scroll")
                self.assertEqual(steps[index - 2]["payload"]["target"], "vaultBrowseGrid")
                self.assertEqual(steps[index - 1]["payload"].get("prop"), "moving")
                self.assertIn({"path": "clippedByWindow", "op": "==", "value": "false"}, step["expect"])

    def test_sibling_virtual_key_uses_cleaned_native_title_slug(self):
        # The observed merged title is acceptance-show. normalizedTitle calls
        # cleanMediaFolderTitle again, whose trailing release-group rule removes
        # '-show'. Preserve that source-derived key instead of guessing from art.
        source = (HERE.parents[1] / "native/engine/VaultKit.cpp").read_text()
        match = re.search(r'trailingGroup\(\s*QStringLiteral\("([^"\n]+)"\)', source)
        self.assertIsNotNone(match)
        trailing_group = json.loads('"' + match.group(1) + '"')
        cleaned = re.sub(trailing_group, ' ', 'acceptance-show').strip()
        slug = re.sub(r'[^a-z0-9]+', ' ', cleaned.lower()).strip()
        self.assertEqual(slug, 'acceptance')
        self.assertIn('const QString slug = normalizedTitle(show.displayTitle);', source)
        self.assertIn('show.key = levelPath + kShowSentinel + slug;', source)
        raw = json.dumps(self.steps('vault_sibling_seasons'))
        self.assertNotIn('::show::acceptance show', raw)
        self.assertIn('${VAULT_SIBLING_ROOT}::show::' + slug, raw)

    def test_player_capture_requires_playback_advance_then_fade_settle(self):
        for name in ("vault_film", "vault_anime", "vault_clips_prepare", "vault_clips_refreshed"):
            steps = self.steps(name)
            for index, step in enumerate(steps):
                if step.get("payload", {}).get("grab", {}).get("target") == "player":
                    self.assertEqual(steps[index - 1].get("presentation_wait_ms"), 350, name)
                    state = steps[index - 2]
                    self.assertEqual(state.get("cmd"), "qml-get", name)
                    for check in ({"path": "props.playbackStarted", "op": "==", "value": "true"},
                                  {"path": "props.starting", "op": "==", "value": "false"},
                                  {"path": "props.playbackPosition", "op": ">", "value": "0.25"},
                                  {"path": "props.statusMsg", "op": "==", "value": ""}):
                        self.assertIn(check, state["expect"], name)

    def test_positive_shutdown_leaves_real_reader_open(self):
        for name in ("reader_comic_shutdown", "reader_comic_baseline_shutdown"):
            steps = self.steps(name)
            self.assertFalse(any(step.get("payload", {}).get("target") == "hudBackAction" for step in steps))
            self.assertTrue(any(step["cmd"] == "ui-wait-for" and step["payload"].get("prop")
                                in ("contentRendered", "rightPixelsShown") for step in steps))
        baseline = json.dumps(self.steps("reader_comic_baseline_shutdown"))
        self.assertNotIn("readerCommand_layout", baseline)


class FixtureMutationSafetyTests(unittest.TestCase):
    def setUp(self):
        self.fixtures = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(self.fixtures)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "owned"
        self.outside = self.base / "outside"
        self.root.mkdir()
        self.outside.mkdir()
        self.marker = {"schema": "colosseum.acceptance119.fixtures.v1",
                       "source_sha": self.fixtures.SOURCE_SHA,
                       "root": self.root.as_posix()}
        self.write_marker()

    def write_marker(self):
        (self.root / self.fixtures.OWNER).write_text(json.dumps(self.marker))

    def outside_clip(self):
        file = self.outside / "clips/loose/clip-gamma.mp4"
        file.parent.mkdir(parents=True)
        file.write_bytes(b"outside data must survive")
        return file

    def test_vault_ancestor_symlink_cannot_delete_outside_file(self):
        outside = self.outside_clip()
        (self.root / "vault").symlink_to(self.outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "vault-remove-added-clip")
        self.assertEqual(outside.read_bytes(), b"outside data must survive")

    def test_comic_ancestor_symlink_cannot_rename_outside_file(self):
        outside = self.outside / "twin.cbz"
        outside.write_bytes(b"outside comic")
        (self.root / "comics").mkdir()
        (self.root / "comics/a").symlink_to(self.outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "comic-a-missing")
        self.assertEqual(outside.read_bytes(), b"outside comic")
        self.assertFalse((self.outside / "twin.cbz.unavailable").exists())

    def test_media_source_symlink_cannot_import_outside_bytes(self):
        outside = self.outside / "secret.mp4"
        outside.write_bytes(b"outside input")
        (self.root / "media").mkdir()
        (self.root / "media/reference.mp4").symlink_to(outside)
        (self.root / "vault/clips/loose").mkdir(parents=True)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "vault-add-clip")
        self.assertFalse((self.root / "vault/clips/loose/clip-gamma.mp4").exists())
        self.assertEqual(outside.read_bytes(), b"outside input")

    def test_leaf_symlink_is_rejected_even_when_unlink_would_only_remove_link(self):
        outside = self.outside_clip()
        leaf = self.root / "vault/clips/loose/clip-gamma.mp4"
        leaf.parent.mkdir(parents=True)
        leaf.symlink_to(outside)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "vault-remove-added-clip")
        self.assertTrue(leaf.is_symlink())
        self.assertTrue(outside.is_file())

    def test_source_sha_and_exact_root_are_required(self):
        for changed in ({"source_sha": "0"*40}, {"root": self.outside.as_posix()},
                        {"root": None}, {"source_sha": None}):
            with self.subTest(changed=changed):
                original = dict(self.marker)
                self.marker.update(changed)
                self.write_marker()
                with self.assertRaises(ValueError):
                    self.fixtures.assert_owned(self.root)
                self.marker = original
        self.write_marker()

    def test_marker_symlink_is_not_ownership(self):
        marker = self.root / self.fixtures.OWNER
        marker.rename(self.outside / "marker.json")
        marker.symlink_to(self.outside / "marker.json")
        with self.assertRaises(ValueError):
            self.fixtures.assert_owned(self.root)

    def test_root_alias_symlink_is_rejected_before_resolve(self):
        alias = self.base / "alias"
        alias.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.fixtures.assert_owned(alias)

    def test_inside_root_symlink_is_also_rejected(self):
        real = self.root / "real"
        real.mkdir()
        (self.root / "vault").symlink_to(real, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.fixtures.safe_fixture_path(self.root, "vault/new.mp4")

    def test_safe_path_rejects_traversal_and_outside_absolute_paths(self):
        for path in ("../outside/data", self.outside / "data"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.fixtures.safe_fixture_path(self.root, path)
        self.assertEqual(self.fixtures.safe_fixture_path(self.root, "safe/new.mp4"), self.root / "safe/new.mp4")

    @unittest.skipUnless(os.name == "nt", "Actual Windows junction requires Windows receiving runtime")
    def test_windows_junction_cannot_delete_outside_clip(self):
        outside = self.outside_clip()
        junction = self.root / "vault"
        subprocess.run(["cmd", "/c", "mklink", "/J", str(junction), str(self.outside)], check=True, capture_output=True)
        self.addCleanup(lambda: os.rmdir(junction) if junction.exists() else None)
        with self.assertRaises(ValueError):
            self.fixtures.apply_mutation(self.root, "vault-remove-added-clip")
        self.assertTrue(outside.is_file())


if __name__ == "__main__":
    unittest.main()

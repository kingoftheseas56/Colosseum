"""Regression coverage for SHA-256-pinned optional source archive media."""
import importlib.util
import json
import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "restore_source_archive_media", ROOT / "scripts/restore_source_archive_media.py")
restore_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(restore_module)
MANIFEST = json.loads((ROOT / "scripts/source_archive_media_manifest.json").read_text(encoding="utf-8"))


class TestSourceArchiveMedia(unittest.TestCase):
    def test_only_manifest_media_is_archive_excluded(self):
        if not (ROOT / ".git").exists():
            self.skipTest("Git metadata unavailable in downloaded source ZIP")
        tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
        response = subprocess.check_output(
            ["git", "check-attr", "-z", "--stdin", "export-ignore"],
            input=tracked, cwd=ROOT)
        fields = response.split(b"\0")
        excluded = {fields[i].decode("utf-8") for i in range(0, len(fields) - 1, 3)
                    if fields[i + 2] == b"set"}
        self.assertEqual(excluded, {row["path"] for row in MANIFEST["files"]})

    def test_pinned_media_matches_checkout(self):
        self.assertEqual(MANIFEST["version"], 1)
        self.assertGreaterEqual(len(MANIFEST["files"]), 35)
        self.assertEqual(len({row["path"] for row in MANIFEST["files"]}), len(MANIFEST["files"]))
        scopes = {row["scope"] for row in MANIFEST["files"]}
        self.assertEqual(scopes, set(restore_module.SCOPES))
        for row in MANIFEST["files"]:
            self.assertTrue(restore_module._verify(ROOT / row["path"], row), row["path"])

    def test_offline_restore_from_full_checkout(self):
        entries = [next(row for row in MANIFEST["files"] if row["scope"] == scope)
                   for scope in restore_module.SCOPES]
        with tempfile.TemporaryDirectory() as temporary:
            result = restore_module.restore(
                Path(temporary), source_dir=ROOT, offline=True,
                manifest={**MANIFEST, "files": entries})
            self.assertEqual(result["restored"], len(entries))
            self.assertEqual(result["bytesRestored"], sum(e["bytes"] for e in entries))
            again = restore_module.restore(
                Path(temporary), offline=True, verify_only=True,
                manifest={**MANIFEST, "files": entries})
            self.assertEqual(again["verified"], len(entries))

    def test_corrupt_existing_media_is_not_overwritten(self):
        row = next(r for r in MANIFEST["files"] if r["scope"] == "release")
        with tempfile.TemporaryDirectory() as temporary:
            file = Path(temporary) / row["path"]
            file.parent.mkdir(parents=True)
            file.write_bytes(b"not original media")
            with self.assertRaisesRegex(ValueError, "differs"):
                restore_module.restore(
                    temporary, source_dir=ROOT, offline=True,
                    manifest={**MANIFEST, "files": [row]})
            self.assertEqual(file.read_bytes(), b"not original media")

    def test_offline_missing_media_fails(self):
        row = next(r for r in MANIFEST["files"] if r["scope"] == "lanista")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(FileNotFoundError, "Offline media unavailable"):
                restore_module.restore(
                    temporary, offline=True, manifest={**MANIFEST, "files": [row]})

    def test_invalid_manifest_path_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            for path in ("../outside", "/absolute", "tests/../../outside", "C:\\temp\\file"):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    restore_module._target(Path(temporary), path)


if __name__ == "__main__":
    unittest.main()

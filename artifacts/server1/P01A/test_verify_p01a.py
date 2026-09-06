import hashlib
import json
import subprocess
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path

from artifacts.server1.P01A.verify_p01a import ValidationError, validate_hashes, validate_libtorrent


ROOT = Path(__file__).resolve().parents[3]
LOCK = ROOT / "docs" / "server1" / "DEPENDENCY-LOCK.json"
SUBSTRATE = ROOT / "docs" / "server1" / "NATIVE-SUBSTRATE.json"
DUPLICATED_CONSUMER = ROOT / "artifacts" / "server1" / "P01A" / "consumer" / "CMakeLists.txt"


class VerifyP01ATests(unittest.TestCase):
    def _load(self, path):
        self.assertTrue(path.is_file(), f"missing required evidence file: {path}")
        return json.loads(path.read_text(encoding="utf-8"))

    def test_native_lock_freezes_libtorrent_2_0_and_shared_target(self):
        lock = self._load(LOCK)
        self.assertEqual(lock["schema"], "colosseum-server1-dependency-lock/v2")
        self.assertTrue(all(record["identity"] == "git_blob" for record in lock["tracked_files"]))
        self.assertTrue(all(record["identity"] == "raw_disk" for record in lock["external_files"]))
        self.assertEqual(lock["libtorrent"]["header_declared"]["major"], 2)
        self.assertEqual(lock["libtorrent"]["header_declared"]["minor"], 0)
        self.assertNotIn("revision", lock["libtorrent"]["linked_runtime"])
        self.assertEqual(lock["libtorrent"]["local_patch_provenance"]["state"], "unknown")
        self.assertEqual(lock["native_authority"]["target"], "colosseum_libtorrent")

    def test_consumer_proves_success_and_required_dependency_failure(self):
        lock = self._load(LOCK)
        consumer = lock["consumer_probe"]
        self.assertFalse(DUPLICATED_CONSUMER.exists(), "duplicate colosseum_libtorrent definition remains")
        self.assertEqual(consumer["source"], "native/CMakeLists.txt")
        self.assertEqual(consumer["target"], "torrent_engine_link_harness")
        self.assertEqual(consumer["configure_valid"]["exit"], 0)
        self.assertEqual(consumer["build"]["exit"], 0)
        self.assertEqual(consumer["run"]["exit"], 0)
        self.assertNotEqual(consumer["configure_without_libtorrent"]["exit"], 0)
        self.assertIn("libtorrent", consumer["configure_without_libtorrent"]["output"])

    def test_substrate_records_toolchain_and_linux_lane(self):
        substrate = self._load(SUBSTRATE)
        self.assertEqual(substrate["schema"], "colosseum-server1-native-substrate/v2")
        self.assertEqual(substrate["compiler"]["language_standard"], "C++17")
        self.assertEqual(substrate["compiler"]["crt"], "/MD")
        self.assertEqual(substrate["linux_qualification"]["state"], "planned")
        self.assertTrue(substrate["linux_qualification"]["lane"])

    def test_libtorrent_abi_mismatch_is_rejected(self):
        lock = self._load(LOCK)
        mutated = deepcopy(lock)
        mutated["libtorrent"]["abi"]["architecture"] = "x86"
        with self.assertRaisesRegex(ValidationError, "ABI"):
            validate_libtorrent(mutated)

    def test_libtorrent_2_1_and_header_library_mismatch_are_rejected(self):
        lock = self._load(LOCK)

        version_2_1 = deepcopy(lock)
        version_2_1["libtorrent"]["header_declared"].update(
            {"version": "2.1.0.0", "major": 2, "minor": 1}
        )
        version_2_1["libtorrent"]["linked_runtime"]["version"] = "2.1.0.0"
        with self.assertRaisesRegex(ValidationError, "minor"):
            validate_libtorrent(version_2_1)

        mismatched = deepcopy(lock)
        mismatched["libtorrent"]["linked_runtime"]["version"] = "2.0.10.0"
        with self.assertRaisesRegex(ValidationError, "header/library"):
            validate_libtorrent(mismatched)

    def test_tracked_identity_accepts_crlf_checkout_but_rejects_content_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.txt"
            subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
            subprocess.run(["git", "config", "core.autocrlf", "true"], cwd=root, check=True)
            fixture.write_bytes(b"alpha\nbeta\n")
            subprocess.run(
                ["git", "add", "fixture.txt"], cwd=root, check=True, capture_output=True
            )
            blob_oid = subprocess.run(
                ["git", "rev-parse", ":fixture.txt"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            record = {
                "path": "fixture.txt",
                "identity": "git_blob",
                "git_blob_oid": blob_oid,
                "canonical_bytes": 11,
                "canonical_sha256": hashlib.sha256(b"alpha\nbeta\n").hexdigest(),
            }

            fixture.write_bytes(b"alpha\r\nbeta\r\n")
            validate_hashes(root, [record])

            fixture.write_bytes(b"alpha\r\nchanged\r\n")
            with self.assertRaisesRegex(ValidationError, "tracked file mismatch"):
                validate_hashes(root, [record])

    def test_external_identity_retains_raw_disk_hashing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "external.bin"
            fixture.write_bytes(b"external\r\nbytes\x00")
            record = {
                "path": "external.bin",
                "identity": "raw_disk",
                "bytes": 16,
                "sha256": "13e8b709fdfbeaa3bb5674082b6222388cda4970d7e4480059a6e5c426601fd6",
            }
            validate_hashes(root, [record])

            fixture.write_bytes(b"external\nbytes\x00")
            with self.assertRaisesRegex(ValidationError, "external file mismatch"):
                validate_hashes(root, [record])


if __name__ == "__main__":
    unittest.main(verbosity=2)

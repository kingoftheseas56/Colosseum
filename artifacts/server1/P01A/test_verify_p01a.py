import json
import unittest
from copy import deepcopy
from pathlib import Path

from artifacts.server1.P01A.verify_p01a import ValidationError, validate_libtorrent


ROOT = Path(__file__).resolve().parents[3]
LOCK = ROOT / "docs" / "server1" / "DEPENDENCY-LOCK.json"
SUBSTRATE = ROOT / "docs" / "server1" / "NATIVE-SUBSTRATE.json"


class VerifyP01ATests(unittest.TestCase):
    def _load(self, path):
        self.assertTrue(path.is_file(), f"missing required evidence file: {path}")
        return json.loads(path.read_text(encoding="utf-8"))

    def test_native_lock_freezes_libtorrent_2_0_and_shared_target(self):
        lock = self._load(LOCK)
        self.assertEqual(lock["schema"], "colosseum-server1-dependency-lock/v1")
        self.assertEqual(lock["libtorrent"]["compile_time"]["major"], 2)
        self.assertEqual(lock["libtorrent"]["compile_time"]["minor"], 0)
        self.assertEqual(lock["libtorrent"]["linked_runtime"]["version"], lock["libtorrent"]["compile_time"]["version"])
        self.assertEqual(lock["native_authority"]["target"], "colosseum_libtorrent")

    def test_consumer_proves_success_and_required_dependency_failure(self):
        lock = self._load(LOCK)
        consumer = lock["consumer_probe"]
        self.assertEqual(consumer["configure_valid"]["exit"], 0)
        self.assertEqual(consumer["build"]["exit"], 0)
        self.assertEqual(consumer["run"]["exit"], 0)
        self.assertNotEqual(consumer["configure_without_libtorrent"]["exit"], 0)
        self.assertIn("libtorrent", consumer["configure_without_libtorrent"]["output"])

    def test_substrate_records_toolchain_and_linux_lane(self):
        substrate = self._load(SUBSTRATE)
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


if __name__ == "__main__":
    unittest.main(verbosity=2)

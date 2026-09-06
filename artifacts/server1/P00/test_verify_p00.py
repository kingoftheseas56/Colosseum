import hashlib
import shutil
import tempfile
import unittest
from pathlib import Path

from verify_p00 import ValidationError, validate_identity, validate_manifest


class VerifyP00Tests(unittest.TestCase):
    def _fixture(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        (root / "payload.txt").write_bytes(b"payload")
        digest = hashlib.sha256(b"payload").hexdigest()
        manifest = root / "SHA256SUMS.txt"
        manifest.write_text(f"{digest}  7  payload.txt\n", encoding="utf-8")
        return root, manifest

    def test_one_byte_oracle_mutation_is_rejected_before_runtime(self):
        root, _ = self._fixture()
        oracle = root / "oracle.js"
        oracle.write_bytes(b"oracle")
        mutated = root / "mutated.js"
        mutated.write_bytes(b"oraclf")
        runtime_marker = root / "runtime-started"
        with self.assertRaises(ValidationError):
            validate_identity(mutated, "v4.21.1", "4.21.0", hashlib.sha256(b"oracle").hexdigest(), 6)
        self.assertFalse(runtime_marker.exists())

    def test_mismatched_embedded_version_is_rejected(self):
        root, _ = self._fixture()
        oracle = root / "oracle.js"
        oracle.write_text('module.exports = { name: "stremio-server", version: "4.21.0" };', encoding="utf-8")
        digest = hashlib.sha256(oracle.read_bytes()).hexdigest()
        with self.assertRaises(ValidationError):
            validate_identity(oracle, "v4.21.1", "4.21.1", digest, oracle.stat().st_size)
        self.assertEqual(validate_identity(oracle, "v4.21.1", "4.21.0", digest, oracle.stat().st_size)["embedded_version"], "4.21.0")

    def test_manifest_reports_missing_duplicate_escape_and_unmanifested_paths(self):
        root, manifest = self._fixture()
        (root / "extra.txt").write_bytes(b"extra")
        with self.assertRaisesRegex(ValidationError, "unmanifested"):
            validate_manifest(root, manifest)

        manifest.write_text(manifest.read_text(encoding="utf-8") + manifest.read_text(encoding="utf-8"), encoding="utf-8")
        with self.assertRaisesRegex(ValidationError, "duplicate"):
            validate_manifest(root, manifest)

        manifest.write_text("""aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  1  ../escape.txt\n""", encoding="utf-8")
        with self.assertRaisesRegex(ValidationError, "escaping"):
            validate_manifest(root, manifest)

        manifest.write_text("""aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  1  missing.txt\n""", encoding="utf-8")
        with self.assertRaisesRegex(ValidationError, "missing"):
            validate_manifest(root, manifest)


if __name__ == "__main__":
    unittest.main()

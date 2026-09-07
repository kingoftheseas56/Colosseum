from pathlib import Path
import os
import tempfile
import unittest

from tools.server_lab.adapters.stremio import Bundle, ORACLE_SHA256, runtime_version, sha256_file


class ReferenceIdentityTests(unittest.TestCase):
    def test_oracle_hash_rejects_mutation(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name in ("runtime", "ffmpeg", "ffprobe"):
                (root / name).write_bytes(b"x")
            server = root / "server.js"
            server.write_bytes(b"not-the-oracle")
            bundle = Bundle(root / "runtime", server, root / "ffmpeg", root / "ffprobe")
            with self.assertRaisesRegex(RuntimeError, "oracle hash mismatch"):
                bundle.validate()

    def test_official_runtime_bundle_identity_when_required(self):
        required = os.environ.get("SERVER1_P02_REQUIRE_RUNTIME") == "1"
        names = {
            "runtime": os.environ.get("SERVER1_STREMIO_RUNTIME"),
            "server": os.environ.get("SERVER1_STREMIO_SERVER"),
            "ffmpeg": os.environ.get("SERVER1_STREMIO_FFMPEG"),
            "ffprobe": os.environ.get("SERVER1_STREMIO_FFPROBE"),
        }
        if not required and not all(names.values()):
            self.skipTest("official runtime bundle not supplied")
        self.assertTrue(all(names.values()), names)
        bundle = Bundle(*(Path(names[k]) for k in ("runtime", "server", "ffmpeg", "ffprobe")))
        bundle.validate()
        self.assertEqual(sha256_file(bundle.server), ORACLE_SHA256)
        self.assertTrue(runtime_version(bundle.runtime))


if __name__ == "__main__":
    unittest.main()

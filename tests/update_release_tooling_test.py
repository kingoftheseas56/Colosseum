#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys
import shutil

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "update"))
sys.path.insert(0, str(ROOT / "scripts"))

from generate_update_manifest import manifest_bytes, sign_raw  # noqa: E402
from verify_update_release import verify  # noqa: E402
from publish_app_release import publish_draft  # noqa: E402

OPENSSL = shutil.which("openssl") or "C:/Program Files/Git/usr/bin/openssl.exe"


class FakeGitHub:
    def __init__(self, published: bool = False):
        self.published = published
        self.calls = []

    def __call__(self, path, token, payload=None, method=None, raw_url=None,
                 content_type="application/json", data=None):
        self.calls.append((path, method, content_type, data))
        if method is None and path.endswith("/releases/tags/v1.1.0"):
            if self.published:
                return {"id": 7, "draft": False, "assets": [], "html_url": "published"}
            raise RuntimeError("not found")
        if method == "POST" and path.endswith("/releases"):
            return {"id": 7, "draft": True, "upload_url": "https://upload.test/assets{?name}",
                    "assets": [], "html_url": "draft"}
        if method == "POST" and raw_url:
            return {"id": len(self.calls), "name": raw_url.split("name=", 1)[1]}
        if method == "DELETE":
            return {}
        raise AssertionError((path, method, raw_url))


class UpdateReleaseToolingTests(unittest.TestCase):
    def test_deterministic_sign_verify_and_mutation_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            installer = root / "Colosseum-1.1.0-setup.exe"
            installer.write_bytes(b"tiny installer payload")
            presentation = root / "presentation.json"
            presentation.write_text(json.dumps({
                "eyebrow": "A NEW CHAPTER IS READY",
                "title": "Colosseum 1.1",
                "summary": "Signed update.",
                "notes_url": "https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.0",
                "highlights": [{"kind": "feature", "section": "COLOSSEUM",
                                "title": "Current", "body": "Verified."}],
                "artwork": [],
            }), encoding="utf-8")
            key = root / "signing.pem"
            public = root / "public.der"
            subprocess.run([OPENSSL, "genpkey", "-algorithm", "ED25519", "-out", str(key)], check=True)
            subprocess.run([OPENSSL, "pkey", "-in", str(key), "-pubout", "-outform", "DER",
                            "-out", str(public)], check=True)
            first = manifest_bytes("1.1.0", installer, presentation)
            self.assertEqual(first, manifest_bytes("1.1.0", installer, presentation))
            signature = sign_raw(first, key)
            manifest_path = root / "colosseum-update-v1.json"
            signature_path = root / "colosseum-update-v1.json.sig"
            manifest_path.write_bytes(first)
            signature_path.write_bytes(signature)
            result = verify(manifest_path, signature_path, installer, public)
            self.assertEqual(result["version"], "1.1.0")
            mutated = bytearray(signature)
            mutated[0] ^= 1
            signature_path.write_bytes(mutated)
            with self.assertRaises(Exception):
                verify(manifest_path, signature_path, installer, public)

    def test_publisher_creates_draft_with_exact_mime_and_refuses_published(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            installer = root / "Colosseum-1.1.0-setup.exe"
            installer.write_bytes(b"installer")
            manifest = root / "colosseum-update-v1.json"
            manifest.write_text(json.dumps({"artwork": []}), encoding="utf-8")
            (root / "colosseum-update-v1.json.sig").write_bytes(b"sig")
            fake = FakeGitHub()
            publish_draft("v1.1.0", installer, root, "token", fake)
            uploads = [call for call in fake.calls if call[1] == "POST" and call[3] is not None]
            self.assertEqual([call[2] for call in uploads], [
                "application/vnd.microsoft.portable-executable", "application/json", "application/octet-stream"
            ])
            with self.assertRaises(SystemExit):
                publish_draft("v1.1.0", installer, root, "token", FakeGitHub(published=True))

    def test_schema_and_bootstrap_presentation_are_present(self):
        schema = json.loads((ROOT / "scripts/update/update-manifest-v1.schema.json").read_text(encoding="utf-8"))
        presentation = json.loads((ROOT / "release/presentation/1.1.0.json").read_text(encoding="utf-8"))
        self.assertEqual(schema["$id"].split("/")[-1], "update-manifest-v1.schema.json")
        self.assertEqual(presentation["notes_url"], "https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.0")


if __name__ == "__main__":
    unittest.main()

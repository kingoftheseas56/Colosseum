#!/usr/bin/env python3
"""Offline tests of the independent 1.1.9 feed publisher and signing boundary."""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "update"))

from create_stable_feed import asset_url, build_stable_feed, validated_base  # noqa: E402
from generate_update_manifest import manifest_bytes, sign_raw  # noqa: E402
from verify_update_release import verify  # noqa: E402

OPENSSL = shutil.which("openssl") or "C:/Program Files/Git/usr/bin/openssl.exe"
ASSET_BASE = ("https://gitlab.com/kingoftheseas56/colosseum-site/-/raw/"
              "release-assets/v1.1.9/releases?ref_type=heads")


class IndependentUpdateFeedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="colosseum-stable-feed-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.installer = self.root / "Colosseum-1.1.9-setup.exe"
        self.installer.write_bytes(b"a tiny Windows installer test fixture")
        self.private_key = self.root / "transient-test-private.pem"
        self.public_der = self.root / "test-public.der"
        subprocess.run([OPENSSL, "genpkey", "-algorithm", "ED25519",
                        "-out", str(self.private_key)], check=True, capture_output=True)
        subprocess.run([OPENSSL, "pkey", "-in", str(self.private_key),
                        "-pubout", "-outform", "DER", "-out", str(self.public_der)],
                       check=True, capture_output=True)
        self.presentation = self.root / "presentation.json"
        self.manifest = self.root / "colosseum-update-v1.json"
        self.signature = self.root / "colosseum-update-v1.json.sig"
        self._present()

    def _present(self, **changes):
        presentation = {
            "eyebrow": "A NEW CHAPTER IS READY",
            "title": "Colosseum 1.1.9",
            "summary": "Updates from the first-party endpoint.",
            "notes_url": "https://getcolosseum.world/releases/v1.1.9",
            "highlights": [],
            "artwork": [],
        }
        presentation.update(changes)
        self.presentation.write_text(json.dumps(presentation), encoding="utf-8")
        self.manifest.write_bytes(manifest_bytes("1.1.9", self.installer, self.presentation))
        self.signature.write_bytes(sign_raw(self.manifest.read_bytes(), self.private_key))

    def feed(self, **kwargs) -> bytes:
        return build_stable_feed(
            manifest=self.manifest, signature=self.signature, installer=self.installer,
            public_key_der=self.public_der, asset_base_url=kwargs.pop("asset_base_url", ASSET_BASE),
            **kwargs)

    def test_deterministic_and_exact_metadata(self):
        first = self.feed()
        self.assertEqual(first, self.feed())
        data = json.loads(first)
        self.assertEqual(data["tag_name"], "v1.1.9")
        self.assertIs(data["draft"], False)
        self.assertIs(data["prerelease"], False)
        self.assertEqual([x["name"] for x in data["assets"]], [
            "colosseum-update-v1.json", "colosseum-update-v1.json.sig",
            "Colosseum-1.1.9-setup.exe",
        ])
        for item in data["assets"]:
            self.assertTrue(item["browser_download_url"].startswith(
                "https://gitlab.com/kingoftheseas56/colosseum-site/-/raw/"
                "release-assets%2Fv1.1.9/releases/"))
            self.assertTrue(item["browser_download_url"].endswith("?ref_type=heads"))
            local = self.root / item["name"]
            self.assertEqual(item["size"], local.stat().st_size)
            self.assertEqual(item["digest"], "sha256:" + hashlib.sha256(local.read_bytes()).hexdigest())
        self.assertIn(b'"draft":false', first)

    def test_1110_encodes_branch_slash_for_public_lfs_downloads(self):
        # Full unauthenticated downloads of this encoded-ref URL returned the
        # verified installer; the unencoded branch path returned HTTP404.
        base = ASSET_BASE.replace('v1.1.9', 'v1.1.10')
        self.assertEqual(
            asset_url(validated_base(base, 'v1.1.10'), 'Colosseum-1.1.10-setup.exe'),
            'https://gitlab.com/kingoftheseas56/colosseum-site/-/raw/'
            'release-assets%2Fv1.1.10/releases/Colosseum-1.1.10-setup.exe?ref_type=heads')

    def test_rejects_altered_signature(self):
        corrupt = bytearray(self.signature.read_bytes())
        corrupt[0] ^= 1
        self.signature.write_bytes(corrupt)
        with self.assertRaises(subprocess.CalledProcessError):
            self.feed()

    def test_rejects_altered_installer(self):
        self.installer.write_bytes(b"installer tampered after signing")
        with self.assertRaisesRegex(ValueError, "installer size/digest mismatch"):
            self.feed()

    def test_rejects_mutated_signed_manifest(self):
        self.manifest.write_bytes(self.manifest.read_bytes().replace(b"1.1.9", b"1.1.8", 1))
        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
            self.feed()

    def test_rejects_wrong_public_key(self):
        another_public = self.root / "wrong-public.der"
        another_private = self.root / "wrong-private.pem"
        subprocess.run([OPENSSL, "genpkey", "-algorithm", "ED25519",
                        "-out", str(another_private)], check=True, capture_output=True)
        subprocess.run([OPENSSL, "pkey", "-in", str(another_private),
                        "-pubout", "-outform", "DER", "-out", str(another_public)],
                       check=True, capture_output=True)
        with self.assertRaises(subprocess.CalledProcessError):
            build_stable_feed(manifest=self.manifest, signature=self.signature,
                              installer=self.installer, public_key_der=another_public,
                              asset_base_url=ASSET_BASE)

    def test_rejects_wrong_base_host_branch_and_scheme(self):
        invalid = (
            ASSET_BASE.replace("gitlab.com", "evil.example"),
            ASSET_BASE.replace("https://", "http://"),
            ASSET_BASE.replace("v1.1.9/", "v1.2.0/"),
            ASSET_BASE.replace("release-assets/", "not-releases/"),
            ASSET_BASE + "&skipCheck=1",
            ASSET_BASE.replace("gitlab.com", "user:secret@gitlab.com"),
        )
        for base in invalid:
            with self.subTest(base=base):
                with self.assertRaises(ValueError):
                    self.feed(asset_base_url=base)

    def test_artwork_requires_verified_files_and_lists_their_digests(self):
        art = self.root / "cover.png"
        art.write_bytes(b"fake image bytes as a digest fixture")
        self._present(artwork=[{"asset": "cover.png", "path": str(art)}],
                      highlights=[{"kind": "feature", "section": "UPDATES",
                                   "title": "Cover", "body": "Hash checked.",
                                   "artwork_assets": ["cover.png"]}])
        with self.assertRaisesRegex(ValueError, "artwork directory is required"):
            self.feed()
        data = json.loads(self.feed(artwork_dir=self.root))
        self.assertEqual(data["assets"][-1]["name"], "cover.png")
        self.assertEqual(data["assets"][-1]["size"], len(art.read_bytes()))
        art.write_bytes(b"tampered image")
        with self.assertRaisesRegex(ValueError, "artwork digest mismatch"):
            self.feed(artwork_dir=self.root)

    def test_old_github_notes_remain_valid_and_other_hosts_fail(self):
        self._present(notes_url="https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.1.9")
        self.assertEqual(verify(self.manifest, self.signature, self.installer,
                                self.public_der)["version"], "1.1.9")
        self.assertEqual(json.loads(self.feed())["tag_name"], "v1.1.9")
        self.presentation.write_text(json.dumps({
            "eyebrow": "NEW", "title": "Colosseum", "summary": "Release notes test",
            "notes_url": "https://getcolosseum.world.evil.example/releases/v1.1.9",
            "highlights": [], "artwork": []}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "wrong notes URL"):
            manifest_bytes("1.1.9", self.installer, self.presentation)

    def test_cli_creates_once_without_upload_or_overwrite(self):
        target = self.root / "updates" / "stable.json"
        cmd = [sys.executable, str(ROOT / "scripts/update/create_stable_feed.py"),
               "--manifest", str(self.manifest), "--signature", str(self.signature),
               "--installer", str(self.installer), "--public-key-der", str(self.public_der),
               "--asset-base-url", ASSET_BASE, "--output", str(target)]
        first = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(target.read_bytes(), self.feed())
        second = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(second.returncode, 0, "must not overwrite a reviewed feed")
        self.assertEqual(target.read_bytes(), self.feed())


if __name__ == "__main__":
    unittest.main()

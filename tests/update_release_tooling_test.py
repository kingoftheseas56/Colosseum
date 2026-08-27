#!/usr/bin/env python3
from __future__ import annotations

import json
import hashlib
import os
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
from verify_update_release import verify, verify_signature  # noqa: E402
from publish_app_release import publish_draft  # noqa: E402
from generate_installed_chronicle import generate  # noqa: E402

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

    def test_real_presentation_hashes_all_five_artwork_and_rejects_missing_reference(self):
        presentation_path = ROOT / "release/presentation/1.1.0.json"
        presentation = json.loads(presentation_path.read_text(encoding="utf-8"))
        expected_assets = [
            "colosseum-reader.png",
            "colosseum-tankoban-discover.png",
            "colosseum-biblio.png",
            "colosseum-theatre.png",
            "colosseum-house.png",
        ]
        previous_cwd = Path.cwd()
        os.chdir(ROOT)
        try:
            with tempfile.TemporaryDirectory() as temp:
                installer = Path(temp) / "Colosseum-1.1.0-setup.exe"
                installer.write_bytes(b"slice-4 dummy installer")
                manifest = json.loads(manifest_bytes("1.1.0", installer, presentation_path))

                self.assertEqual([item["asset"] for item in manifest["artwork"]], expected_assets)
                self.assertEqual({tuple(item.keys()) for item in manifest["artwork"]},
                                 {("asset", "sha256")})
                self.assertEqual(
                    {asset for highlight in manifest["highlights"]
                     for asset in highlight.get("artworkAssets", [])},
                    set(expected_assets),
                )
                for item in manifest["artwork"]:
                    artwork = ROOT / "release/presentation/artwork" / item["asset"]
                    self.assertTrue(artwork.is_file())
                    self.assertEqual(item["sha256"], hashlib.sha256(artwork.read_bytes()).hexdigest())

                forbidden = {"path", "command", "qml", "html", "executable"}
                def assert_safe(value):
                    if isinstance(value, dict):
                        self.assertTrue(forbidden.isdisjoint(value), value)
                        for child in value.values():
                            assert_safe(child)
                    elif isinstance(value, list):
                        for child in value:
                            assert_safe(child)
                assert_safe(manifest)

                broken = dict(presentation)
                broken["artwork"] = presentation["artwork"][1:]
                broken_path = Path(temp) / "broken-presentation.json"
                broken_path.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "highlight references missing artwork"):
                    manifest_bytes("1.1.0", installer, broken_path)
        finally:
            os.chdir(previous_cwd)


    def test_nsis_uses_product_branding_and_official_icon(self):
        script = (ROOT / "scripts/installer/colosseum.nsi").read_text(encoding="utf-8")
        self.assertIn('Name "Colosseum"', script)
        self.assertIn('Caption "Colosseum"', script)
        self.assertIn('UninstallCaption "Colosseum"', script)
        self.assertIn('!define MUI_WELCOMEPAGE_TITLE "Colosseum"', script)
        self.assertIn('!define MUI_ICON "${STAGE}\\assets\\icons\\colosseum.ico"', script)
        self.assertIn('!define MUI_UNICON "${STAGE}\\assets\\icons\\colosseum.ico"', script)
        self.assertTrue((ROOT / "assets/icons/colosseum.ico").is_file())



class InstalledChronicleGeneratorTests(unittest.TestCase):
    """Slice 1: the bundled installed-chronicle generator reuses the exact manifest +
    signing trust path, copies all five verified screenshots, and rejects missing
    artwork references. Uses a throwaway test key — the production-signed artifacts
    are produced by a maintainer run, not by this test."""

    PRESENTATION = ROOT / "release/presentation/1.1.0.json"
    EXPECTED_ASSETS = [
        "colosseum-reader.png",
        "colosseum-tankoban-discover.png",
        "colosseum-biblio.png",
        "colosseum-theatre.png",
        "colosseum-house.png",
    ]

    def _make_keypair(self, temp: Path) -> tuple[Path, Path]:
        key = temp / "signing.pem"
        public = temp / "public.der"
        subprocess.run([OPENSSL, "genpkey", "-algorithm", "ED25519", "-out", str(key)], check=True)
        subprocess.run([OPENSSL, "pkey", "-in", str(key), "-pubout", "-outform", "DER",
                        "-out", str(public)], check=True)
        return key, public

    def test_generates_signed_bundle_with_five_verified_artwork(self):
        previous_cwd = Path.cwd()
        os.chdir(ROOT)
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                key, public = self._make_keypair(root)
                out = root / "installed-chronicle"
                result = generate(
                    presentation=self.PRESENTATION,
                    version="1.1.0",
                    private_key=key,
                    output_dir=out,
                )
                # Outputs present.
                manifest_path = Path(result["MANIFEST_PATH"])
                sig_path = Path(result["SIGNATURE_PATH"])
                self.assertTrue(manifest_path.is_file())
                self.assertTrue(sig_path.is_file())
                # Artwork copied and hash-matches the manifest digest.
                manifest = json.loads(manifest_path.read_bytes())
                artwork_dir = out / "artwork"
                self.assertEqual([item["asset"] for item in manifest["artwork"]],
                                 self.EXPECTED_ASSETS)
                for item in manifest["artwork"]:
                    copied = artwork_dir / item["asset"]
                    self.assertTrue(copied.is_file(), f"missing copied artwork: {item['asset']}")
                    self.assertEqual(hashlib.sha256(copied.read_bytes()).hexdigest(),
                                     item["sha256"])
                # Every highlight artwork reference resolves.
                declared = {item["asset"] for item in manifest["artwork"]}
                for highlight in manifest["highlights"]:
                    for ref in highlight.get("artworkAssets", []):
                        self.assertIn(ref, declared, "highlight references missing artwork")
                # Signature verifies against the keypair's public key.
                verify_signature(manifest_path, sig_path, public)
                # No forbidden key reaches the manifest (installer block is inert text
                # the gallery ignores; it must not carry a path/command/executable).
                forbidden = {"path", "command", "qml", "html", "executable"}
                stack = [manifest]
                while stack:
                    node = stack.pop()
                    if isinstance(node, dict):
                        self.assertTrue(forbidden.isdisjoint(node), node)
                        stack.extend(node.values())
                    elif isinstance(node, list):
                        stack.extend(node)
                # Inert installer block present (schema-required, gallery-ignored).
                self.assertEqual(set(manifest["installer"]), {"asset", "size", "sha256"})
        finally:
            os.chdir(previous_cwd)

    def test_rejects_highlight_referencing_missing_artwork(self):
        """Negative control: a chapter that still names a removed artwork declaration
        must fail generation (the missing-reference check fires in manifest_bytes)."""
        previous_cwd = Path.cwd()
        os.chdir(ROOT)
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                key, public = self._make_keypair(root)
                broken = json.loads(self.PRESENTATION.read_text(encoding="utf-8"))
                # Drop the reader artwork declaration while the Reader chapter still refs it.
                broken["artwork"] = [item for item in broken["artwork"]
                                     if item["asset"] != "colosseum-reader.png"]
                broken_path = root / "broken-presentation.json"
                broken_path.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "highlight references missing artwork"):
                    generate(
                        presentation=broken_path,
                        version="1.1.0",
                        private_key=key,
                        output_dir=root / "out",
                    )
        finally:
            os.chdir(previous_cwd)


if __name__ == "__main__":
    unittest.main()

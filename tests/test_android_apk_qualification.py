#!/usr/bin/env python3
"""WT-12: fixture-only tests for scripts/android/qualify_apk.py. Synthetic APK ZIPs and ELF
headers; no Android build, signing, install or CI dispatch."""
from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "android"))
import qualify_apk  # noqa: E402


def elf(machine=183, align=16384, cls=2):
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4] = cls
    header[5] = 1
    struct.pack_into("<H", header, 18, machine)
    struct.pack_into("<Q", header, 32, 64)        # e_phoff
    struct.pack_into("<HH", header, 54, 56, 1)    # e_phentsize, e_phnum
    ph = bytearray(56)
    struct.pack_into("<I", ph, 0, 1)              # PT_LOAD
    struct.pack_into("<Q", ph, 48, align)
    return bytes(header + ph)


def make_apk(path: Path, libs=None, extra=None, signing_block=False):
    libs = libs if libs is not None else {"lib/arm64-v8a/libcolosseum_arm64-v8a.so": elf()}
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("AndroidManifest.xml", b"binary-manifest")
        for name, blob in libs.items():
            zf.writestr(name, blob)
        for name, blob in (extra or {}).items():
            zf.writestr(name, blob)
    if signing_block:
        data = path.read_bytes()
        eocd = data.rfind(b"PK\x05\x06")
        cd_offset = struct.unpack_from("<I", data, eocd + 16)[0]
        payload = b"\x00" * 8
        size = len(payload) + 8 + 16
        block = struct.pack("<Q", size) + payload + struct.pack("<Q", size) + qualify_apk.APK_SIG_MAGIC
        new = bytearray(data[:cd_offset] + block + data[cd_offset:])
        new_eocd = len(data[:cd_offset] + block) + (eocd - cd_offset)
        struct.pack_into("<I", new, new_eocd + 16, cd_offset + len(block))
        path.write_bytes(bytes(new))
    return path


class InspectTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_clean_unsigned_arm64(self):
        apk = make_apk(self.dir / "a.apk")
        receipt = qualify_apk.inspect_apk(str(apk), require_unsigned=True)
        self.assertEqual(receipt["abis"], ["arm64-v8a"])
        self.assertEqual(receipt["signedState"], "unsigned")
        self.assertEqual(receipt["updateCompatibleWithInstalledSigner"], "UNVERIFIED")
        self.assertEqual(receipt["apkSha256"], hashlib.sha256(apk.read_bytes()).hexdigest())

    def test_signing_is_reported_and_optionally_rejected(self):
        v1 = make_apk(self.dir / "v1.apk", extra={"META-INF/CERT.RSA": b"x", "META-INF/CERT.SF": b"x"})
        self.assertEqual(qualify_apk.inspect_apk(str(v1))["signedState"], "signed")
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(v1), require_unsigned=True)
        v2 = make_apk(self.dir / "v2.apk", signing_block=True)
        receipt = qualify_apk.inspect_apk(str(v2))
        self.assertTrue(receipt["signingBlockPresent"])
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(v2), require_unsigned=True)
        v4 = make_apk(self.dir / "v4.apk")
        (self.dir / "v4.apk.idsig").write_bytes(b"sig")
        self.assertTrue(qualify_apk.inspect_apk(str(v4))["externalV4SignaturePresent"])

    def test_rejects_wrong_abi_arch_alignment_and_missing_libs(self):
        cases = {
            "x86": {"lib/x86_64/libcolosseum.so": elf(machine=62)},
            "mixed": {"lib/arm64-v8a/libcolosseum.so": elf(), "lib/armeabi-v7a/libc.so": elf()},
            "arch": {"lib/arm64-v8a/libcolosseum.so": elf(machine=62)},
            "elf32": {"lib/arm64-v8a/libcolosseum.so": elf(cls=1)},
            "align4k": {"lib/arm64-v8a/libcolosseum.so": elf(align=4096)},
            "none": {},
        }
        for label, libs in cases.items():
            with self.subTest(label=label), self.assertRaises(ValueError):
                qualify_apk.inspect_apk(str(make_apk(self.dir / f"{label}.apk", libs=libs)))

    def test_rejects_malformed_or_manifestless_zip(self):
        bad = self.dir / "bad.apk"
        bad.write_bytes(b"not a zip")
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(bad))
        nomanifest = self.dir / "nm.apk"
        with zipfile.ZipFile(nomanifest, "w") as zf:
            zf.writestr("lib/arm64-v8a/libc.so", elf())
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(nomanifest))

    def test_engine_member_must_match_prefix(self):
        engine = elf() + b"engine-v0.1.5"
        lib = self.dir / "libnuvio_engine.so"
        lib.write_bytes(engine)
        good = make_apk(self.dir / "e.apk", libs={"lib/arm64-v8a/libcolosseum.so": elf(),
                                                   qualify_apk.ENGINE_MEMBER: engine})
        receipt = qualify_apk.inspect_apk(str(good), engine_library=str(lib))
        self.assertEqual(receipt["engineNativeLibrary"]["sha256"], hashlib.sha256(engine).hexdigest())
        absent = make_apk(self.dir / "noe.apk")
        self.assertFalse(qualify_apk.inspect_apk(str(absent))["engineMemberPresent"])
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(absent), engine_library=str(lib))
        other = make_apk(self.dir / "oe.apk", libs={qualify_apk.ENGINE_MEMBER: elf() + b"other"})
        with self.assertRaises(ValueError):
            qualify_apk.inspect_apk(str(other), engine_library=str(lib))


class IdentityTests(unittest.TestCase):
    badging = "package: name='world.getcolosseum.app.beta' versionCode='42' versionName='1.2.0' platformBuildVersionName='16'\n"

    def test_beta_identity(self):
        identity = qualify_apk.parse_aapt_identity(self.badging)
        self.assertEqual(qualify_apk.check_identity(identity, qualify_apk.BETA_PACKAGE, "42", "1.2.0")["versionCode"], 42)

    def test_identity_mismatches_and_lanes(self):
        identity = qualify_apk.parse_aapt_identity(self.badging)
        for kwargs in ({"package": "world.getcolosseum.app"}, {"package": "com.example.app"},
                       {"package": qualify_apk.BETA_PACKAGE, "version_code": "43"},
                       {"package": qualify_apk.BETA_PACKAGE, "version_name": "1.2.1"},
                       {"package": qualify_apk.BETA_PACKAGE, "version_code": "0"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                qualify_apk.check_identity(identity, kwargs.pop("package"), **kwargs)
        with self.assertRaises(ValueError):
            qualify_apk.parse_aapt_identity("no package line")
        with self.assertRaises(ValueError):
            qualify_apk.validate_version_code("2100000001")


class SnapshotAndProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name) / "repo"
        self.repo.mkdir()
        run = lambda *a: subprocess.run(["git", "-C", str(self.repo), *a], check=True, capture_output=True)
        run("init", "-q")
        run("config", "user.email", "fixture@example.invalid")
        run("config", "user.name", "Fixture")
        (self.repo / "Main.qml").write_text("Item {}\n")
        run("add", "Main.qml")
        run("commit", "-qm", "fixture")

    def tearDown(self):
        self.tmp.cleanup()

    def test_clean_dirty_and_overlay_composition(self):
        clean = qualify_apk.source_snapshot(str(self.repo))
        self.assertTrue(clean["trackedSourceClean"])
        (self.repo / "Main.qml").write_text("Item { width: 1 }\n")
        dirty = qualify_apk.source_snapshot(str(self.repo))
        self.assertFalse(dirty["trackedSourceClean"])
        self.assertEqual(dirty["dirtyTrackedFiles"], ["Main.qml"])
        manifest = Path(self.tmp.name) / "overlay.json"
        digest = hashlib.sha256((self.repo / "Main.qml").read_bytes()).hexdigest()
        manifest.write_text(json.dumps({"files": [{"path": "Main.qml", "sha256": digest}]}))
        overlay = qualify_apk.source_snapshot(str(self.repo), str(manifest))
        self.assertEqual(overlay["overlay"]["files"][0]["sha256"], digest)
        manifest.write_text(json.dumps({"files": [{"path": "Main.qml", "sha256": "0" * 64}]}))
        with self.assertRaises(ValueError):
            qualify_apk.source_snapshot(str(self.repo), str(manifest))
        manifest.write_text(json.dumps({"files": [{"path": "../escape", "sha256": "0" * 64}]}))
        with self.assertRaises(ValueError):
            qualify_apk.source_snapshot(str(self.repo), str(manifest))

    def test_provenance_requires_stable_source_and_matching_receipt(self):
        apk = make_apk(Path(self.tmp.name) / "p.apk")
        receipt = qualify_apk.inspect_apk(str(apk))
        identity = qualify_apk.parse_aapt_identity(IdentityTests.badging)
        before = qualify_apk.source_snapshot(str(self.repo))
        result = qualify_apk.provenance(str(apk), receipt, identity, before, before)
        self.assertEqual(result["sourceComposition"], "clean-commit")
        self.assertEqual(result["deviceAcceptance"], "LOCAL_PENDING")
        (self.repo / "Main.qml").write_text("changed during build\n")
        after = qualify_apk.source_snapshot(str(self.repo))
        with self.assertRaises(ValueError):
            qualify_apk.provenance(str(apk), receipt, identity, before, after)
        with self.assertRaises(ValueError):
            qualify_apk.provenance(str(apk), {**receipt, "apkSha256": "0" * 64}, identity, after, after)

    def test_cli_round_trip(self):
        apk = make_apk(Path(self.tmp.name) / "c.apk")
        out = Path(self.tmp.name) / "inspect.json"
        code = qualify_apk.main(["inspect", "--apk", str(apk), "--output", str(out)])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out.read_text())["signedState"], "unsigned")
        bad = Path(self.tmp.name) / "bad.apk"
        bad.write_bytes(b"x")
        self.assertEqual(qualify_apk.main(["inspect", "--apk", str(bad), "--output", str(out)]), 2)


if __name__ == "__main__":
    unittest.main()

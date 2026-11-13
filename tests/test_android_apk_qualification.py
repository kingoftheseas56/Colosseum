#!/usr/bin/env python3
"""WT-12: fixture-only tests for scripts/android/qualify_apk.py. Synthetic APK ZIPs and ELF
headers; no Android build, signing, install or CI dispatch."""
from __future__ import annotations

import contextlib
import hashlib
import io
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

APP_LIBRARY = "lib/arm64-v8a/libcolosseum_arm64-v8a.so"
HOST_LIBRARY = "lib/arm64-v8a/libcolosseum_nuvio_host_arm64-v8a.so"


def elf(machine=183, align=16384, cls=2, offset=0, vaddr=0, filesz=120, memsz=120):
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4] = cls
    header[5] = 1
    struct.pack_into("<H", header, 18, machine)
    struct.pack_into("<Q", header, 32, 64)        # e_phoff
    struct.pack_into("<HH", header, 54, 56, 1)    # e_phentsize, e_phnum
    ph = bytearray(56)
    struct.pack_into("<I", ph, 0, 1)              # PT_LOAD
    struct.pack_into("<QQ", ph, 8, offset, vaddr)
    struct.pack_into("<QQ", ph, 32, filesz, memsz)
    struct.pack_into("<Q", ph, 48, align)
    return bytes(header + ph)


def elf_with_sections(section_names=()):
    blob = bytearray(elf())
    names = bytearray(b"\0")
    section_name_offsets = []
    for name in (".shstrtab", *section_names):
        section_name_offsets.append(len(names))
        names.extend(name.encode("ascii") + b"\0")

    section_count = 2 + len(section_names)
    section_offset = len(blob)
    names_offset = section_offset + section_count * 64
    headers = [bytes(64)]
    headers.append(struct.pack(
        "<IIQQQQIIQQ", section_name_offsets[0], 3, 0, 0, names_offset,
        len(names), 0, 0, 1, 0,
    ))
    for name, name_offset in zip(section_names, section_name_offsets[1:]):
        section_type = 2 if name == ".symtab" else 1
        headers.append(struct.pack(
            "<IIQQQQIIQQ", name_offset, section_type, 0, 0, 0, 0,
            0, 0, 1, 0,
        ))

    blob.extend(b"".join(headers))
    blob.extend(names)
    struct.pack_into("<Q", blob, 40, section_offset)
    struct.pack_into("<HHH", blob, 58, 64, section_count, 1)
    return bytes(blob)


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

    def inspect_cli(self, apk, label, require_stripped=False, engine_library=None):
        output = self.dir / f"{label}.json"
        args = ["inspect", "--apk", str(apk)]
        if engine_library:
            args.extend(["--engine-library", str(engine_library)])
        if require_stripped:
            args.append("--require-stripped-app-libs")
        args.extend(["--output", str(output)])
        stdout, stderr = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                status = qualify_apk.main(args)
        except SystemExit as exc:
            status = exc.code
        receipt = json.loads(output.read_text(encoding="utf-8")) if output.exists() else None
        return status, receipt, stdout.getvalue(), stderr.getvalue()

    def test_required_stripped_app_libraries_accept_clean_pair_and_leave_engine_exempt(self):
        app = elf_with_sections()
        host = elf_with_sections()
        engine = elf_with_sections((".debug_info", ".symtab"))
        engine_path = self.dir / "libnuvio_engine.so"
        engine_path.write_bytes(engine)
        apk = make_apk(self.dir / "stripped-pair.apk", libs={
            APP_LIBRARY: app,
            HOST_LIBRARY: host,
            "lib/arm64-v8a/libnuvio_engine.so": engine,
        })

        status, receipt, _, error = self.inspect_cli(
            apk, "stripped-pair", require_stripped=True, engine_library=engine_path,
        )

        self.assertEqual(status, 0, error)
        self.assertEqual(receipt["engineNativeLibrary"]["sha256"], hashlib.sha256(engine).hexdigest())
        self.assertEqual({item["path"] for item in receipt["nativeLibraries"]}, {
            APP_LIBRARY, HOST_LIBRARY, "lib/arm64-v8a/libnuvio_engine.so",
        })

    def test_required_stripped_app_libraries_reject_debug_and_symbol_tables(self):
        bad_sections = (".debug_info", ".zdebug_info", ".symtab")
        for member in (APP_LIBRARY, HOST_LIBRARY):
            for section in bad_sections:
                with self.subTest(member=member, section=section):
                    libs = {APP_LIBRARY: elf_with_sections(), HOST_LIBRARY: elf_with_sections()}
                    libs[member] = elf_with_sections((section,))
                    apk = make_apk(self.dir / f"reject-{member.rsplit('/', 1)[-1]}-{section[1:]}.apk",
                                   libs=libs)

                    status, _, _, error = self.inspect_cli(
                        apk, f"reject-{member.rsplit('/', 1)[-1]}-{section[1:]}",
                        require_stripped=True,
                    )

                    self.assertEqual(status, 2)
                    self.assertIn("unstripped section", error)
                    self.assertIn(section, error)

    def test_required_stripped_app_libraries_require_both_members(self):
        clean = elf_with_sections()
        cases = (
            ("app", {HOST_LIBRARY: clean}, APP_LIBRARY),
            ("host", {APP_LIBRARY: clean}, HOST_LIBRARY),
        )
        for label, libs, missing_member in cases:
            with self.subTest(missing=missing_member):
                apk = make_apk(self.dir / f"missing-{label}.apk", libs=libs)

                status, _, _, error = self.inspect_cli(
                    apk, f"missing-{label}", require_stripped=True,
                )

                self.assertEqual(status, 2)
                self.assertIn("required app native library", error)
                self.assertIn(missing_member, error)

    def test_required_stripped_app_libraries_reject_malformed_section_bounds(self):
        valid = bytearray(elf_with_sections((".safe",)))
        section_offset = struct.unpack_from("<Q", valid, 40)[0]
        malformed = {}

        bad_table = bytearray(valid)
        struct.pack_into("<Q", bad_table, 40, len(bad_table) - 16)
        malformed["section_table"] = (bytes(bad_table), "section header table bounds")

        bad_string_table = bytearray(valid)
        struct.pack_into("<Q", bad_string_table, section_offset + 64 + 24, len(bad_string_table) - 2)
        struct.pack_into("<Q", bad_string_table, section_offset + 64 + 32, 16)
        malformed["name_table"] = (bytes(bad_string_table), "section name string table bounds")

        bad_name_offset = bytearray(valid)
        struct.pack_into("<I", bad_name_offset, section_offset + 2 * 64, 0xFFFFFFFF)
        malformed["name_offset"] = (bytes(bad_name_offset), "section name offset")

        for label, (app, message) in malformed.items():
            with self.subTest(label=label):
                apk = make_apk(self.dir / f"malformed-{label}.apk", libs={
                    APP_LIBRARY: app,
                    HOST_LIBRARY: elf_with_sections(),
                })

                status, _, _, error = self.inspect_cli(
                    apk, f"malformed-{label}", require_stripped=True,
                )

                self.assertEqual(status, 2)
                self.assertIn(message, error.lower())

    def test_stripped_app_library_requirement_is_off_by_default(self):
        apk = make_apk(self.dir / "legacy-app-only.apk", libs={APP_LIBRARY: elf()})

        status, receipt, _, error = self.inspect_cli(apk, "legacy-default")

        self.assertEqual(status, 0, error)
        self.assertEqual([item["path"] for item in receipt["nativeLibraries"]], [APP_LIBRARY])

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

    def test_rejects_invalid_load_segment_alignment_and_bounds(self):
        cases = {
            "zero_alignment": (elf(align=0), "alignment"),
            "non_power_of_two": (elf(align=24576), "power of two"),
            "incongruent_16k": (elf(vaddr=4096), "incongruent"),
            # Congruent modulo 16 KiB, but not the declared 32 KiB alignment.
            "incongruent_32k": (elf(align=32768, vaddr=16384), "incongruent"),
            "filesz_exceeds_memsz": (elf(memsz=119), "file size exceeds memory size"),
            "file_end_outside_blob": (elf(filesz=121, memsz=121), "file range"),
            "file_start_outside_blob": (elf(offset=16384, filesz=1, memsz=1), "file range"),
        }
        for label, (blob, message) in cases.items():
            with self.subTest(label=label), self.assertRaisesRegex(ValueError, message):
                apk = make_apk(self.dir / f"{label}.apk", libs={"lib/arm64-v8a/libc.so": blob})
                qualify_apk.inspect_apk(str(apk))

    def test_accepts_valid_file_backed_and_memory_only_load_segments(self):
        cases = {
            "nonzero_congruent_offsets": elf(offset=64, vaddr=16448, filesz=56, memsz=128),
            "larger_alignment": elf(align=65536),
            # No bytes are read from p_offset when p_filesz is zero.
            "memory_only": elf(offset=16384, vaddr=32768, filesz=0, memsz=4096),
            "empty": elf(filesz=0, memsz=0),
        }
        for label, blob in cases.items():
            with self.subTest(label=label):
                apk = make_apk(self.dir / f"{label}.apk", libs={"lib/arm64-v8a/libc.so": blob})
                receipt = qualify_apk.inspect_apk(str(apk))
                self.assertEqual(receipt["nativeLibraries"][0]["bytes"], len(blob))

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

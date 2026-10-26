#!/usr/bin/env python3
from __future__ import annotations
import struct
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "android"))
import release_apk

class ReleaseConfigTests(unittest.TestCase):
    def base(self):
        return {"COLOSSEUM_ANDROID_APK_FLAVOUR": "release-unsigned",
                "COLOSSEUM_NUVIO_ENGINE_PREFIX": "C:/engine-prefix"}

    def test_debug_defaults_keep_existing_paths_and_names(self):
        cfg = release_apk.resolve_build_config("x86_64", "/build", "/out", {})
        self.assertEqual(cfg.flavour, "debug")
        self.assertEqual(cfg.build_dir, str(Path("/build") / "x86_64"))
        self.assertEqual(cfg.artifact_name, "colosseum-x86_64-debug.apk")

    def test_release_is_arm64_preview_staging_and_separate(self):
        cfg = release_apk.resolve_build_config("arm64-v8a", "/build", "/out", self.base())
        self.assertEqual(cfg.build_dir, str(Path("/build") / "release-unsigned" / "arm64-v8a"))
        self.assertEqual(cfg.artifact_name, "colosseum-arm64-v8a-release-unsigned.apk")
        self.assertEqual(cfg.package, release_apk.DEFAULT_PACKAGE)
        self.assertEqual(cfg.version_code, 2)
        self.assertEqual(cfg.endpoint, release_apk.DEFAULT_ENDPOINT)
        args = release_apk.release_cmake_args(cfg)
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", args)
        self.assertIn("-DQT_ANDROID_SIGN_APK=OFF", args)
        self.assertIn("-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON", args)

    def test_release_validation_rejects_bad_mode_inputs_and_signing(self):
        cases = [
            ("x86", "supported only for arm64", {"abi": "x86_64"}),
            ("unknown flavour", "unsupported APK flavour", {"env": {"COLOSSEUM_ANDROID_APK_FLAVOUR": "release"}}),
            ("missing engine", "requires COLOSSEUM_NUVIO_ENGINE_PREFIX", {"env": {"COLOSSEUM_ANDROID_APK_FLAVOUR": "release-unsigned"}}),
            ("bad package", "release package", {"env": {**self.base(), "COLOSSEUM_ANDROID_PREVIEW_PACKAGE": "org.example.app"}}),
            ("bad version", "version code", {"env": {**self.base(), "COLOSSEUM_ANDROID_VERSION_CODE": "0"}}),
            ("bad endpoint", "staging origin", {"env": {**self.base(), "COLOSSEUM_CONTENT_API_URL": "https://example.invalid"}}),
            ("override build type", "forbids overriding", {"env": {**self.base(), "COLOSSEUM_ANDROID_CMAKE_ARGS": "-DCMAKE_BUILD_TYPE=Debug"}}),
        ]
        for label, message, options in cases:
            with self.subTest(label=label):
                abi = options.pop("abi", "arm64-v8a")
                env = options.get("env", self.base())
                with self.assertRaisesRegex(ValueError, message):
                    release_apk.resolve_build_config(abi, "/build", "/out", env)
        for name in release_apk.SIGNING_ENV:
            with self.subTest(signing_variable=name):
                env = {**self.base(), name: "must-not-be-used"}
                with self.assertRaisesRegex(ValueError, "forbids signing"):
                    release_apk.resolve_build_config("arm64-v8a", "/build", "/out", env)

    def test_dispatch_modes_are_exclusive(self):
        from contextlib import redirect_stderr
        import io
        with redirect_stderr(io.StringIO()):
            self.assertEqual(release_apk.main(["validate-dispatch", "--diagnostic", "--release"]), 2)
        with redirect_stderr(io.StringIO()):
            self.assertEqual(release_apk.main([
                "validate-dispatch", "--release", "--package", "bad.package",
                "--version-code", "2", "--endpoint", release_apk.DEFAULT_ENDPOINT
            ]), 2)

    def test_workflow_release_is_opt_in_and_has_no_signing_inputs(self):
        workflow = (ROOT / ".github" / "workflows" / "android.yml").read_text(encoding="utf-8")
        inputs = workflow.split("      release_unsigned_arm64:", 1)[1].split("      release_preview_package:", 1)[0]
        self.assertIn("default: false", inputs)
        step = workflow.split("- name: Build unsigned arm64 Release preview APK", 1)[1].split(
            "- name: Write unsigned Release provenance manifest", 1
        )[0]
        self.assertIn("if: " + "$" + "{{ inputs.release_unsigned_arm64 }}", step)
        self.assertNotIn("secrets.", step)
        self.assertIn("release-unsigned", step)
        self.assertIn("QT_ANDROID_SIGN_APK", step)
        self.assertIn("android-deps-release-arm64-v1-", workflow)
        dependency_step = workflow.split("- name: Build OpenSSL / Boost / libtorrent (cache miss only)", 1)[1].split(
            "- name: Save built dependency prefixes", 1)[0]
        self.assertIn('scripts/android/build_deps.sh arm64-v8a', dependency_step)
        self.assertIn('scripts/android/build_deps.sh arm64-v8a x86_64', dependency_step)
        self.assertIn('if [[ "$RELEASE_MODE" == "true" ]]', dependency_step)
        self.assertIn("source-before.json", workflow)
        self.assertIn("source-after.json", workflow)

    def test_builder_passes_the_verified_canonical_engine_to_required_verifier(self):
        builder = (ROOT / "scripts" / "android" / "build_apk.sh").read_text(encoding="utf-8")
        helper = (ROOT / "scripts" / "android" / "release_apk.py").read_text(encoding="utf-8")
        self.assertIn(
            '--engine-library "$COLOSSEUM_NUVIO_ENGINE_PREFIX/android/$ABI/lib/libnuvio_engine.so"',
            builder)
        self.assertIn('verify.add_argument("--engine-library", required=True)', helper)

    def test_qt_output_selector_requires_one_known_output(self):
        from contextlib import redirect_stderr, redirect_stdout
        import io
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with redirect_stderr(io.StringIO()):
                self.assertEqual(release_apk.main(["select-qt-apk", "--build-dir", str(root)]), 2)
            expected = root / "android-build" / "colosseum.apk"
            expected.parent.mkdir(parents=True)
            expected.write_bytes(b"apk")
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(release_apk.main(["select-qt-apk", "--build-dir", str(root)]), 0)
            self.assertEqual(output.getvalue().strip(), str(expected))
            other = root / "android-build-colosseum" / "colosseum.apk"
            other.parent.mkdir(parents=True)
            other.write_bytes(b"apk")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(release_apk.main(["select-qt-apk", "--build-dir", str(root)]), 2)

    def test_project_version_reads_actual_native_cmake_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "native").mkdir()
            (root / "native" / "CMakeLists.txt").write_text(
                "project(colosseum VERSION 1.1.10 LANGUAGES C CXX)\n", encoding="utf-8")
            self.assertEqual(release_apk.project_version(str(root)), "1.1.10")
            (root / "native" / "CMakeLists.txt").write_text("project(other VERSION 2.0)\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "no literal colosseum project VERSION"):
                release_apk.project_version(str(root))

class SourceSnapshotTests(unittest.TestCase):
    def test_source_metadata_snapshot_detects_tracked_changes_and_records_run_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "config", "user.email", "test@example.invalid"], check=True)
            subprocess.run(["git", "-C", str(root), "config", "user.name", "Test"], check=True)
            source = root / "tracked.txt"
            source.write_text("before\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "tracked.txt"], check=True)
            subprocess.run(["git", "-C", str(root), "commit", "-qm", "fixture"], check=True)
            env = {**os.environ, "GITHUB_REF": "refs/heads/release-fixture", "GITHUB_RUN_ID": "12345"}
            before = release_apk._source_snapshot(str(root), env)
            self.assertEqual(before["branchRef"], "refs/heads/release-fixture")
            self.assertEqual(before["runId"], "12345")
            (root / "before.json").write_text("untracked receipt\n", encoding="utf-8")
            unchanged = release_apk._source_snapshot(str(root), env)
            self.assertEqual(unchanged["sourceTreeOid"], before["sourceTreeOid"])
            self.assertEqual(unchanged["sourceIndexMetadataSha256"],
                             before["sourceIndexMetadataSha256"])
            source.write_text("after\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "clean tracked source"):
                release_apk._source_snapshot(str(root), env)

class UnsignedApkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.apk = self.root / "sample.apk"
        self.engine = self.root / "libnuvio_engine.so"
        self.engine.write_bytes(self._elf())
        self._write_apk(self.apk)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def _elf(align=16384, machine=183, elf_class=2):
        blob = bytearray(120)
        blob[:16] = b"\x7fELF" + bytes([elf_class, 1, 1]) + bytes(9)
        struct.pack_into("<HHIQQQIHHHHHH", blob, 16,
                         2, machine, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
        struct.pack_into("<IIQQQQQQ", blob, 64, 1, 5, 0, 0, 0, 120, 120, align)
        return bytes(blob)

    def _write_apk(self, path, *, signature=False, align=16384, machine=183, elf_class=2):
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as zf:
            zf.writestr("AndroidManifest.xml", b"binary manifest fixture")
            zf.writestr("lib/arm64-v8a/libfixture.so",
                        self._elf(align=align, machine=machine, elf_class=elf_class))
            zf.writestr("lib/arm64-v8a/libnuvio_engine.so", self._elf())
            if signature:
                zf.writestr("META-INF/CERT.SF", b"fake v1 signature")

    def _insert_signing_block(self):
        data = self.apk.read_bytes()
        old_eocd, cd_offset, _ = release_apk.find_eocd(data)
        pair = struct.pack("<Q", 8) + struct.pack("<I", 0x12345678) + b"test"
        size = 8 + len(pair) + 16
        block = struct.pack("<Q", size) + pair + struct.pack("<Q", size) + release_apk.APK_SIG_MAGIC
        new = bytearray(data[:cd_offset] + block + data[cd_offset:])
        eocd = old_eocd + len(block)
        struct.pack_into("<I", new, eocd + 16, cd_offset + len(block))
        self.apk.write_bytes(new)

    def test_valid_unsigned_arm64_archive(self):
        inspected = release_apk.inspect_unsigned_zip(str(self.apk))
        self.assertEqual(inspected["abis"], ["arm64-v8a"])
        self.assertFalse(inspected["signingBlockPresent"])
        self.assertFalse(inspected["jarSignatureMembersPresent"])
        self.assertEqual(inspected["nativeLibraries"][0]["ptLoadAlignments"], [16384])
        inspected = release_apk.inspect_unsigned_zip(str(self.apk), str(self.engine))
        self.assertEqual(inspected["engineNativeLibrary"]["sha256"],
                         release_apk.hashlib.sha256(self.engine.read_bytes()).hexdigest())

    def test_unexpected_abi_directory_is_rejected_even_without_native_library(self):
        with zipfile.ZipFile(self.apk, "a", compression=zipfile.ZIP_STORED) as zf:
            zf.writestr("lib/x86_64/NOTICE", b"unexpected ABI content")
        with self.assertRaisesRegex(ValueError, "unexpected native ABI in APK: x86_64"):
            release_apk.inspect_unsigned_zip(str(self.apk))

    def test_engine_prefix_hash_must_match_packaged_native_library(self):
        wrong_engine = self.root / "wrong-engine.so"
        wrong_engine.write_bytes(self._elf(align=16384, machine=183))
        wrong_engine.write_bytes(wrong_engine.read_bytes() + b"different")
        with self.assertRaisesRegex(ValueError, "does not match the verified prefix"):
            release_apk.inspect_unsigned_zip(str(self.apk), str(wrong_engine))

    def test_malformed_zip_rejected(self):
        self.apk.write_bytes(b"not a zip")
        with self.assertRaisesRegex(ValueError, "EOCD"):
            release_apk.inspect_unsigned_zip(str(self.apk))

    def test_external_v4_signature_file_is_rejected(self):
        Path(str(self.apk) + ".idsig").write_bytes(b"external signature")
        with self.assertRaisesRegex(ValueError, "external v4"):
            release_apk.inspect_unsigned_zip(str(self.apk))

    def test_v1_signature_member_rejected(self):
        self._write_apk(self.apk, signature=True)
        with self.assertRaisesRegex(ValueError, "JAR/v1"):
            release_apk.inspect_unsigned_zip(str(self.apk))

    def test_v2_signing_block_is_detected_and_rejected(self):
        self._insert_signing_block()
        data = self.apk.read_bytes()
        old_eocd, cd_offset, _ = release_apk.find_eocd(data)
        self.assertTrue(release_apk.has_apk_signing_block(data, cd_offset))
        with self.assertRaisesRegex(ValueError, "Signing Block"):
            release_apk.inspect_unsigned_zip(str(self.apk))

    def test_malformed_signing_block_is_not_misclassified_as_unsigned(self):
        self._insert_signing_block()
        data = bytearray(self.apk.read_bytes())
        _, cd_offset, _ = release_apk.find_eocd(bytes(data))
        struct.pack_into("<Q", data, cd_offset - 24, 1)
        self.apk.write_bytes(data)
        _, cd_offset, _ = release_apk.find_eocd(bytes(data))
        with self.assertRaisesRegex(ValueError, "malformed APK Signing Block"):
            release_apk.has_apk_signing_block(bytes(data), cd_offset)

    def test_elf_abi_class_and_16k_load_alignment_are_gated(self):
        for kwargs, expected in (({"align": 4096}, "16 KiB"),
                                 ({"machine": 62}, "AArch64"),
                                 ({"elf_class": 1}, "ELF64")):
            with self.subTest(kwargs=kwargs):
                self._write_apk(self.apk, **kwargs)
                with self.assertRaisesRegex(ValueError, expected):
                    release_apk.inspect_unsigned_zip(str(self.apk))

    def test_aapt_identity_and_zipalign_are_required(self):
        output = "package: name='org.qtproject.example.colosseum.previewnuvio1' versionCode='2' versionName='1.1.10'\n"
        def fake_run(command, **_kwargs):
            if command[1:3] == ["dump", "badging"]:
                return SimpleNamespace(returncode=0, stdout=output, stderr="")
            return SimpleNamespace(returncode=0, stdout="", stderr="")
        with patch.object(release_apk.subprocess, "run", side_effect=fake_run):
            verified = release_apk.run_unsigned_verification(
                str(self.apk), release_apk.DEFAULT_PACKAGE, 2, "aapt", "zipalign",
                str(self.engine), "1.1.10")
        self.assertEqual(verified["versionName"], "1.1.10")
        self.assertTrue(verified["zipalign16KiB"])
        wrong = output.replace("versionCode='2'", "versionCode='3'")
        with patch.object(release_apk.subprocess, "run",
                          return_value=SimpleNamespace(returncode=0, stdout=wrong, stderr="")):
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                release_apk.run_unsigned_verification(
                    str(self.apk), release_apk.DEFAULT_PACKAGE, 2, "aapt", "zipalign",
                    str(self.engine), "1.1.10")
        wrong_name = output.replace("versionName='1.1.10'", "versionName='1.1.11'")
        with patch.object(release_apk.subprocess, "run",
                          return_value=SimpleNamespace(returncode=0, stdout=wrong_name, stderr="")):
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                release_apk.run_unsigned_verification(
                    str(self.apk), release_apk.DEFAULT_PACKAGE, 2, "aapt", "zipalign",
                    str(self.engine), "1.1.10")
        def fail_align(command, **_kwargs):
            if command[1:3] == ["dump", "badging"]:
                return SimpleNamespace(returncode=0, stdout=output, stderr="")
            return SimpleNamespace(returncode=1, stdout="", stderr="alignment failure")
        with patch.object(release_apk.subprocess, "run", side_effect=fail_align):
            with self.assertRaisesRegex(ValueError, "zipalign"):
                release_apk.run_unsigned_verification(
                    str(self.apk), release_apk.DEFAULT_PACKAGE, 2, "aapt", "zipalign",
                    str(self.engine), "1.1.10")

if __name__ == "__main__":
    unittest.main(verbosity=2)

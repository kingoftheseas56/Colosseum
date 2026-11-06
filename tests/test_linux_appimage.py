"""AppImage policy and fail-closed smoke regression tests; no app/GPU claims."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools/linux-release"
COMMIT = "a" * 40


def load(test, name):
    path = TOOLS / (name + ".py")
    test.assertTrue(path.is_file(), "AppImage implementation missing: " + name)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(TOOLS))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


class AppImagePolicyTests(unittest.TestCase):
    def test_name_uses_identity_version_not_current_release_literal(self):
        mod = load(self, "package_appimage")
        identity = {"version": "7.8.9", "architecture": "x86_64", "source_commit": COMMIT}
        self.assertEqual(mod.image_name(identity), "Colosseum-7.8.9-x86_64.AppImage")
        for bad in ("../escape", "1.2", "1.2.0;echo injected"):
            with self.subTest(version=bad), self.assertRaises(ValueError):
                mod.image_name(dict(identity, version=bad))
        with self.assertRaises(ValueError):
            mod.image_name(dict(identity, architecture="aarch64"))

    def test_abi_rejects_noble_and_unknown_requirements(self):
        mod = load(self, "package_appimage")
        report = {"status": "PASS", "elf_count": 10,
                  "required_symbol_versions_max": {"GLIBC": "2.35"}}
        mod.verify_abi(report)
        for version in ("2.39", "2.100", None, "garbage"):
            bad = copy.deepcopy(report)
            bad["required_symbol_versions_max"]["GLIBC"] = version
            with self.subTest(version=version), self.assertRaises(ValueError):
                mod.verify_abi(bad)
        for field, value in (("status", "FAIL"), ("elf_count", 0)):
            with self.assertRaises(ValueError):
                mod.verify_abi(dict(report, **{field: value}))

    def test_stage_requires_exact_commit_3d_ssl_ffmpeg_and_notices(self):
        mod = load(self, "package_appimage")
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            for relative in mod.REQUIRED_FILES:
                path = stage / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            identity = {"version": "7.8.9", "architecture": "x86_64", "source_commit": COMMIT}
            (stage / "BUILD-IDENTITY.json").write_text(json.dumps(identity))
            notice = {"packages": [{"name": name, "license_files": ["COPYING"]}
                                   for name in mod.REQUIRED_NOTICES]}
            (stage / "licenses/runtime/manifest.json").write_text(json.dumps(notice))
            (stage / "licenses/runtime/COPYING").write_text("unchanged fixture notice")
            self.assertEqual(mod.validate_stage(stage, COMMIT), identity)
            with self.assertRaisesRegex(ValueError, "commit"):
                mod.validate_stage(stage, "b" * 40)
            for relative in ("usr/qml/QtQuick3D/qmldir", "usr/lib/libQt6Quick3D.so.6",
                             "usr/lib/libssl.so.3", "usr/bin/ffmpeg"):
                path = stage / relative
                old = path.read_bytes()
                path.unlink()
                with self.subTest(missing=relative), self.assertRaisesRegex(ValueError, "Missing"):
                    mod.validate_stage(stage, COMMIT)
                path.write_bytes(old)
            notice["packages"] = notice["packages"][1:]
            (stage / "licenses/runtime/manifest.json").write_text(json.dumps(notice))
            with self.assertRaisesRegex(ValueError, "notice"):
                mod.validate_stage(stage, COMMIT)

    def test_appimage_header_rejects_plain_elf_and_wrong_architecture(self):
        mod = load(self, "package_appimage")
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "fixture.AppImage"
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            header[8:11] = b"AI\x02"
            header[18:20] = (62).to_bytes(2, "little")
            image.write_bytes(header)
            mod.validate_image(image)
            for offset, value in ((8, 0), (4, 1), (18, 183)):
                bad = bytearray(header)
                bad[offset] = value
                image.write_bytes(bad)
                with self.subTest(offset=offset), self.assertRaises(ValueError):
                    mod.validate_image(image)

    def test_tool_uses_explicit_runtime_and_no_fuse(self):
        mod = load(self, "package_appimage")
        command = mod.build_command(Path("tool"), Path("runtime"), Path("stage"), Path("output"))
        self.assertEqual(command[:2], ["tool", "--appimage-extract-and-run"])
        self.assertIn("--runtime-file", command)
        self.assertEqual(command[command.index("--runtime-file") + 1], "runtime")
        self.assertEqual(command[-2:], ["stage", "output"])


class AppImageSmokeTests(unittest.TestCase):
    def test_environment_discards_host_sdk_and_uses_disposable_profile(self):
        mod = load(self, "smoke_appimage")
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            env = mod.make_environment(profile, profile / "ipc", {
                "PATH": "/usr/bin", "LD_LIBRARY_PATH": "/sdk/lib", "LD_PRELOAD": "bad.so",
                "QT_PLUGIN_PATH": "/sdk/plugins", "QML_IMPORT_PATH": "/sdk/qml",
                "QTWEBENGINE_DISABLE_SANDBOX": "1", "COLOSSEUM_APPDATA_ROOT": "/real-user",
                "GDK_PIXBUF_MODULE_FILE": "/sdk/cache"})
            for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QML_IMPORT_PATH",
                         "QTWEBENGINE_DISABLE_SANDBOX", "COLOSSEUM_APPDATA_ROOT", "GDK_PIXBUF_MODULE_FILE"):
                self.assertNotIn(name, env)
            self.assertEqual(env["QT_QPA_PLATFORM"], "offscreen")
            self.assertEqual(env["COLOSSEUM_STARTUP_PROBE"], "1")
            self.assertEqual(env["XDG_DATA_HOME"], str(profile / "data"))
            self.assertEqual(env["HOME"], str(profile))

    def test_ready_requires_real_log_marker_window_and_finished_splash(self):
        mod = load(self, "smoke_appimage")
        marker = "COLOSSEUM_STARTUP_MILESTONE milestone=shell-interactive atMs=321"
        state = {"windows": [{"visible": True, "width": 1280, "height": 800}]}
        splash = {"props": {"visible": False}}
        self.assertTrue(mod.ready(marker, state, splash))
        for text in ("", "startup ready", marker.replace("shell-interactive", "first-frame")):
            self.assertFalse(mod.ready(text, state, splash))
        self.assertFalse(mod.ready(marker, {"windows": []}, splash))
        self.assertFalse(mod.ready(marker, state, {"props": {"visible": True}}))
        self.assertFalse(mod.ready(marker, state, {"props": {}}))

    def test_nonzero_timeout_or_unrequested_exit_cannot_pass(self):
        mod = load(self, "smoke_appimage")
        mod.validate_exit(0, True, True)
        for code, readiness, requested in ((124, True, True), (-15, True, True),
                                           (1, True, True), (0, False, True), (0, True, False)):
            with self.subTest(code=code, ready=readiness, requested=requested), self.assertRaises(RuntimeError):
                mod.validate_exit(code, readiness, requested)

    def test_readiness_marker_matches_production_probe(self):
        mod = load(self, "smoke_appimage")
        native = (ROOT / "native/GuiStallProbe.h").read_text(encoding="utf-8")
        self.assertIn('COLOSSEUM_STARTUP_MILESTONE milestone=%1 atMs=%2', native)
        self.assertIn('QStringLiteral("shell-interactive")', native)
        self.assertIn("COLOSSEUM_STARTUP_MILESTONE milestone=shell-interactive", mod.READY_MARKER)


class AppImageWorkflowTests(unittest.TestCase):
    def test_matrix_is_explicit_and_does_not_publish_releases(self):
        path = ROOT / ".github/workflows/release-linux-appimage.yml"
        self.assertTrue(path.is_file(), "AppImage workflow missing")
        text = path.read_text()
        for image in ("fedora:latest", "archlinux:latest", "opensuse/tumbleweed", "debian:stable"):
            self.assertIn(image, text)
        self.assertIn("fail-fast: false", text)
        self.assertIn("ubuntu-22.04", text)
        self.assertIn("if: always()", text)
        self.assertIn("contents: read", text)
        self.assertNotIn("contents: write", text)
        self.assertNotIn("softprops/action-gh-release", text)
        self.assertNotIn("Colosseum-1.2.0", text)



class AppImageLifecycleTests(unittest.TestCase):
    def run_observer(self, exit_code=0, text=None, escaped=False, wait_error=None):
        mod = load(self, "smoke_appimage")
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            log = profile / "startup.log"
            log.write_text(text if text is not None else mod.READY_MARKER + " atMs=12\n")
            process = Mock()
            process.poll.return_value = None
            process.wait.side_effect = wait_error
            process.wait.return_value = exit_code
            commands = []
            def ipc(address, name, payload=None):
                commands.append((name, payload))
                if name == "get-state":
                    return {"windows": [{"visible": True, "width": 100, "height": 100}],
                            "appDataRoot": str(profile.parent if escaped else profile / "data/Colosseum")}
                if name == "qml-get":
                    return {"props": {"visible": False}}
                return {"type": "reply"}
            result = mod.observe_and_quit(process, log, profile / "ipc", profile, 0.01, 0.01, ipc)
            return result, commands

    def test_observer_requires_ready_then_requests_normal_quit(self):
        result, commands = self.run_observer()
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(commands[-1], ("ui-keypress", {"key": "Ctrl+Q"}))

    def test_observer_rejects_missing_marker_even_when_ipc_is_alive(self):
        with self.assertRaisesRegex(RuntimeError, "Startup deadline"):
            self.run_observer(text="IPC listening; first frame only")

    def test_observer_rejects_profile_escape_and_abnormal_shutdown(self):
        with self.assertRaisesRegex(RuntimeError, "disposable"):
            self.run_observer(escaped=True)
        with self.assertRaisesRegex(RuntimeError, "Normal shutdown"):
            self.run_observer(exit_code=-15)
        import subprocess
        with self.assertRaises(subprocess.TimeoutExpired):
            self.run_observer(wait_error=subprocess.TimeoutExpired("app", 0.01))

    def test_cleanup_is_owned_bounded_and_never_promotes_a_pass(self):
        mod = load(self, "smoke_appimage")
        from unittest.mock import Mock, patch
        process = Mock(pid=123)
        process.poll.return_value = None
        receipt = {"status": "FAIL"}
        with patch.object(mod.os, "killpg", create=True) as kill:
            mod.cleanup_process(process, receipt)
        self.assertTrue(receipt["forced_cleanup"])
        self.assertEqual(receipt["status"], "FAIL")
        kill.assert_called_once_with(123, mod.signal.SIGTERM)
        process.wait.assert_called_once_with(timeout=5)

    def test_launcher_carries_packaged_data_and_uses_system_trust(self):
        text = (TOOLS / "AppRun.appimage").read_text()
        for name in ("XDG_DATA_DIRS", "SSL_CERT_FILE", "FONTCONFIG_FILE", "FONTCONFIG_PATH"):
            self.assertIn(name, text)
        self.assertNotIn("QTWEBENGINE_DISABLE_SANDBOX", text)
        self.assertNotIn("--no-sandbox", text)
        self.assertIn('"${SSL_CERT_FILE:-}"', text)

    def test_dispatch_bridge_does_not_run_the_old_deb_job(self):
        text = (ROOT / ".github/workflows/release-linux-deb.yml").read_text()
        self.assertIn("type: boolean", text)
        self.assertIn("if: inputs.receive_run_id == '' && !inputs.appimage_candidate", text)
        self.assertIn("uses: ./.github/workflows/release-linux-appimage.yml", text)



class AppImageSdkTests(unittest.TestCase):
    def test_environment_requires_full_glib_not_only_runtime(self):
        lines = (TOOLS / "appimage-environment.yml").read_text().splitlines()
        self.assertTrue(any(line.strip().split("=", 1)[0] == "- glib" for line in lines))

    def test_sdk_rejects_missing_glib_headers_or_empty_include_directories(self):
        mod = load(self, "prepare_appimage_sdk")
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            required = [prefix / "include/glib-2.0/glib.h", prefix / "lib/glib-2.0/include/glibconfig.h"]
            for path in required:
                path.parent.mkdir(parents=True, exist_ok=True)
            with self.assertRaisesRegex(ValueError, "GLib development"):
                mod.require_glib_headers(prefix)
            for path in required:
                path.write_text("/* test header */")
            mod.require_glib_headers(prefix)
            required[-1].unlink()
            with self.assertRaisesRegex(ValueError, "glibconfig.h"):
                mod.require_glib_headers(prefix)



class AppImagePkgConfigTests(unittest.TestCase):
    def test_mixed_build_selects_host_pkg_config_for_cmake_and_loaders(self):
        script=(TOOLS / "build_appimage.sh").read_text()
        self.assertIn("export PKG_CONFIG=/usr/bin/pkg-config", script)
        self.assertEqual(script.count('"$PKG_CONFIG" --variable='),2)
        self.assertIn("curl pkg-config", (ROOT / ".github/workflows/release-linux-appimage.yml").read_text())

    def test_pkg_config_header_gate_rejects_relocated_and_empty_paths(self):
        mod=load(self,"prepare_appimage_sdk")
        from unittest.mock import patch
        import shlex
        with tempfile.TemporaryDirectory() as directory:
            good="-I" + shlex.quote(Path(directory).as_posix())
            with patch.object(mod.subprocess,"check_output",return_value=good) as query:
                mod.require_pkg_config_headers("/usr/bin/pkg-config")
                self.assertEqual(query.call_args.args[0][0],"/usr/bin/pkg-config")
                self.assertIn("libsecret-1", query.call_args.args[0])
            for output in ("", "-I/nonexistent/colosseum-libsecret-headers"):
                with self.subTest(output=output), patch.object(mod.subprocess,"check_output",return_value=output), self.assertRaises(ValueError):
                    mod.require_pkg_config_headers("/usr/bin/pkg-config")


if __name__ == "__main__":
    unittest.main()

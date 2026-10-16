"""Exercise the actual smoke shell script with controlled adb and a virtual clock.

Run: python3 tests/android_emulator_smoke_test.py
SMOKE_SCRIPT may point to a baseline script to demonstrate regression failures.
No Android SDK, APK, device, network, or real sleeps are used.
"""
import json
import os
import re
import xml.etree.ElementTree as ET
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(os.environ.get("SMOKE_SCRIPT", Path(__file__).resolve().parents[1] /
                              "scripts/android/emulator_smoke.sh"))
FIXTURE = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
root = pathlib.Path(os.environ["FIXTURE_ROOT"])
state_path = root / "state.json"
s = json.loads(state_path.read_text())
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
scenario = os.environ["SCENARIO"]
def save(): state_path.write_text(json.dumps(s))
def log(message, pid=123): print("10-16 01:00:00.000 %s 123 I Qt : %s" % (pid, message))
if name == "date": print(s["time"]); sys.exit()
if name == "sleep": s["time"] += int(args[0]); save(); sys.exit()
if name == "aapt":
    print("package: name='%s'" % os.environ.get("FIXTURE_PACKAGE", "test.colosseum"))
    print("launchable-activity: name='test.Activity'")
    sys.exit()
s["calls"].append([s["time"], args]); save()
t = s["time"]
if args[:2] == ["shell", "getprop"]: print("x86_64")
elif args[:2] == ["shell", "ls"]:
    if scenario == "tombstone" and s.get("launched"): print("tombstone_01")
elif args[:3] == ["shell", "am", "force-stop"]:
    if scenario == "force_stop_failure": sys.exit(1)
elif args == ["logcat", "-c"]:
    if scenario == "clear_failure": sys.exit(1)
    s["cleared"] = True; save()
elif args and args[0] == "install":
    if scenario == "install_failure": print("INSTALL_FAILED"); sys.exit(1)
    print("Success")
elif args[:3] == ["shell", "am", "start"]:
    s["launched"] = True; save()
    if scenario == "launch_exit_failure": print("Status: ok"); sys.exit(1)
    if scenario == "launch_error": print("Error: Activity class does not exist"); sys.exit()
    print("Status: ok")
elif args[:3] == ["shell", "uiautomator", "dump"]:
    if scenario == "onboarding_dump_failure": sys.exit(1)
    print("UI hierarchy dumped")
elif args == ["shell", "wm", "size"]:
    print("Physical size: " + ("320x640" if scenario == "onboarding_real_xml" else "1080x1920"))
elif args == ["shell", "cat", "/sdcard/colosseum-smoke-window.xml"]:
    if scenario == "onboarding_real_xml":
        print(pathlib.Path(os.environ["REAL_UI_FIXTURE"]).read_text()); sys.exit()
    package = "foreign.app" if scenario == "onboarding_foreign" else "test.colosseum"
    bounds = "[100,200][200,300]"
    if scenario == "onboarding_bad_bounds": bounds = "[100,200][2000,3000]"
    if scenario == "onboarding_missing_bounds": bounds = ""
    label = "Sign in" if scenario == "onboarding_missing_label" else "Continue without account"
    if scenario in ("onboarding_combined_label", "onboarding_wrong_id"): label += ", Keep everything local on this device."
    if scenario == "onboarding_label_prefix": label += " later"
    resource_id = "QGuiApplication.Main_QMLTYPE_999.accountWelcomeContinueLocal"
    if scenario == "onboarding_wrong_id": resource_id += "Other"
    if scenario == "onboarding_missing_id": resource_id = ""
    widget_class = "android.widget.TextView" if scenario == "onboarding_wrong_class" else "android.widget.Button"
    visible = "false" if scenario == "onboarding_hidden" else "true"
    enabled = "false" if scenario == "onboarding_disabled" else "true"
    node = ('<node package="%s" class="%s" resource-id="%s" content-desc="%s" '
            'bounds="%s" enabled="%s" visible-to-user="%s" clickable="true" />') % (
                package, widget_class, resource_id, label, bounds, enabled, visible)
    print("<hierarchy>" + node * (2 if scenario == "onboarding_duplicate" else 1) + "</hierarchy>")
elif args[:3] == ["shell", "input", "tap"]:
    if args[3:] != os.environ.get("EXPECTED_TAP", "150 250").split(): sys.exit(4)
    s["tapped"] = True; save()
elif args[:2] == ["shell", "pidof"]:
    if scenario == "no_pid" or (scenario == "delayed" and t < 20): sys.exit(1)
    if scenario == "death" and t >= 4: sys.exit(1)
    if scenario == "restart" and t >= 4: print("456")
    elif scenario == "multiple_pids": print("123 456")
    else: print("123")
elif args and args[0] == "logcat":
    if scenario == "logcat_failure": sys.exit(1)
    if "-b" in args:
        if scenario == "crash_buffer": log("Fatal signal 11")
        sys.exit()
    if scenario == "qml_failure": log("QQmlApplicationEngine failed to load component")
    if scenario == "tls_failure": log("No functional TLS backend")
    if scenario == "java_crash" and t >= 4: log("FATAL EXCEPTION: main")
    if scenario == "native_crash" and t >= 4: log("Fatal signal 11")
    ready = 76 if scenario == "delayed" else 2
    if scenario in ("never_ready", "stale_markers"): ready = 999
    if t >= ready:
        if scenario == "late_observation": s["time"] = t + 10; save()
        pid = 987 if scenario == "foreign_markers" else 123
        if scenario != "missing_main": log("[boot] Main.qml loaded", pid)
        if scenario.startswith("onboarding") and scenario != "onboarding_obscured":
            log("[boot] local onboarding visible", pid)
        if scenario != "missing_world" and (not scenario.startswith("onboarding") or s.get("tapped")):
            log("[boot] android world ready: Theatre", pid)
    if scenario == "stale_markers" and not s.get("cleared"):
        log("[boot] Main.qml loaded")
        log("[boot] android world ready: Theatre")
elif args[:2] == ["exec-out", "screencap"]: print("fixture image")
'''


class AndroidSmokeTest(unittest.TestCase):
    def run_smoke(self, scenario, *, ready_timeout=8, alive=6):
        with tempfile.TemporaryDirectory(prefix="android-smoke-test-") as tmp:
            root = Path(tmp)
            binary = root / "bin"
            binary.mkdir()
            for name in ("adb", "date", "sleep", "aapt"):
                path = binary / name
                path.write_text(FIXTURE)
                path.chmod(0o755)
            sdk = root / "sdk/build-tools/1"
            sdk.mkdir(parents=True)
            (sdk / "aapt").symlink_to(binary / "aapt")
            (root / "state.json").write_text(json.dumps({"time": 0, "calls": []}))
            artifact = root / "artifacts"
            env = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                       ADB=str(binary / "adb"), ANDROID_SDK_ROOT=str(root / "sdk"),
                       FIXTURE_ROOT=str(root), SCENARIO=scenario,
                       SMOKE_READY_TIMEOUT=str(ready_timeout))
            if scenario == "onboarding_real_xml":
                xml_path = Path(__file__).resolve().parent / "fixtures/android/onboarding-qt-ui.xml"
                node = next(n for n in ET.parse(xml_path).iter("node")
                            if n.get("resource-id", "").endswith(".accountWelcomeContinueLocal"))
                x1, y1, x2, y2 = map(int, re.findall(r"\d+", node.get("bounds")))
                env.update(REAL_UI_FIXTURE=str(xml_path), FIXTURE_PACKAGE=node.get("package"),
                           EXPECTED_TAP=f"{(x1+x2)//2} {(y1+y2)//2}")
            result = subprocess.run(["bash", str(SCRIPT), "fixture.apk", str(artifact), str(alive)],
                                    env=env, text=True, capture_output=True, timeout=40)
            state = json.loads((root / "state.json").read_text())
            files = {p.name: p.read_text() for p in artifact.iterdir()}
            return result, state, files

    def assert_failed(self, scenario, message):
        result, _, files = self.run_smoke(scenario)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(message, result.stdout)
        for name in ("logcat.txt", "logcat-crash.txt", "screenshot.png", "activities.txt"):
            self.assertIn(name, files)

    def test_real_qt_combined_accessibility_label(self):
        result, state, _ = self.run_smoke("onboarding_real_xml", alive=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        taps = [args for _, args in state["calls"] if args[:3] == ["shell", "input", "tap"]]
        self.assertEqual(len(taps), 1)
        self.assertGreaterEqual(state["time"], 64)

    def test_combined_label_with_other_generated_type_number(self):
        result, _, _ = self.run_smoke("onboarding_combined_label")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_onboarding_choice_then_full_hold(self):
        result, state, _ = self.run_smoke("onboarding", alive=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        taps = [(time, args) for time, args in state["calls"] if args[:3] == ["shell", "input", "tap"]]
        self.assertEqual(taps, [(2, ["shell", "input", "tap", "150", "250"])])
        self.assertGreaterEqual(state["time"], 64)
        self.assertIn("Selected visible Continue without account", result.stdout)

    def test_onboarding_without_unique_visible_valid_target_fails_closed(self):
        for scenario in ("onboarding_foreign", "onboarding_bad_bounds", "onboarding_missing_bounds",
                         "onboarding_missing_label", "onboarding_hidden", "onboarding_disabled",
                         "onboarding_duplicate", "onboarding_dump_failure", "onboarding_obscured",
                         "onboarding_wrong_id", "onboarding_missing_id", "onboarding_wrong_class",
                         "onboarding_label_prefix"):
            with self.subTest(scenario=scenario):
                result, state, _ = self.run_smoke(scenario)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("READINESS TIMEOUT", result.stdout)
                self.assertFalse(any(args[:3] == ["shell", "input", "tap"] for _, args in state["calls"]))

    def test_onboarding_inspection_bounded_and_only_after_main_loaded(self):
        result, state, _ = self.run_smoke("onboarding_missing_label", ready_timeout=20)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        dumps = [time for time, args in state["calls"] if args[:3] == ["shell", "uiautomator", "dump"]]
        self.assertGreaterEqual(dumps[0], 2)
        self.assertTrue(all(b - a >= 5 for a, b in zip(dumps, dumps[1:])))
        _, no_main, _ = self.run_smoke("missing_main")
        self.assertFalse(any(args[:3] == ["shell", "uiautomator", "dump"] for _, args in no_main["calls"]))

    def test_ready_then_full_hold(self):
        result, state, files = self.run_smoke("ready", alive=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(state["time"], 62)
        self.assertIn("screenshot-ready.png", files)
        self.assertIn("Main.qml loaded and initial world reached", result.stdout)

    def test_cold_extraction_76_seconds_then_full_hold(self):
        result, state, _ = self.run_smoke("delayed", ready_timeout=180, alive=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(state["time"], 136)
        self.assertIn("after 76 s; holding 60 s", result.stdout)

    def test_alive_without_readiness_fails(self):
        self.assert_failed("never_ready", "READINESS TIMEOUT")

    def test_both_current_process_markers_required(self):
        for scenario in ("missing_main", "missing_world", "foreign_markers", "no_pid"):
            with self.subTest(scenario=scenario):
                self.assert_failed(scenario, "READINESS TIMEOUT")

    def test_stale_markers_do_not_pass(self):
        self.assert_failed("stale_markers", "READINESS TIMEOUT")

    def test_late_observation_does_not_extend_readiness_deadline(self):
        self.assert_failed("late_observation", "READINESS TIMEOUT")

    def test_launch_failures(self):
        self.assert_failed("launch_error", "ACTIVITY LAUNCH DID NOT SUCCEED")
        self.assert_failed("launch_exit_failure", "ACTIVITY LAUNCH FAILED")

    def test_pid_stable_through_hold(self):
        for scenario in ("death", "restart"):
            with self.subTest(scenario=scenario):
                self.assert_failed(scenario, "PROCESS DIED OR RESTARTED")

    def test_multiple_processes_rejected(self):
        self.assert_failed("multiple_pids", "AMBIGUOUS PROCESS")

    def test_startup_failures(self):
        for scenario in ("qml_failure", "tls_failure", "java_crash", "native_crash"):
            with self.subTest(scenario=scenario):
                self.assert_failed(scenario, "STARTUP OR RUNTIME FAILURE")

    def test_crash_buffer_and_tombstones(self):
        self.assert_failed("crash_buffer", "native crash in logcat")
        self.assert_failed("tombstone", "new tombstones")

    def test_failed_observation_cannot_pass(self):
        for scenario in ("logcat_failure", "clear_failure", "force_stop_failure", "install_failure"):
            with self.subTest(scenario=scenario):
                self.assert_failed(scenario, "SMOKE FAILED")

    def test_fresh_launch_before_clearing_logs(self):
        result, state, _ = self.run_smoke("ready")
        self.assertEqual(result.returncode, 0, result.stdout)
        calls = [args for _, args in state["calls"]]
        stop = calls.index(["shell", "am", "force-stop", "test.colosseum"])
        clear = calls.index(["logcat", "-c"])
        launch = calls.index(["shell", "am", "start", "-W", "-n", "test.colosseum/test.Activity"])
        self.assertLess(stop, clear)
        self.assertLess(clear, launch)


if __name__ == "__main__":
    unittest.main()

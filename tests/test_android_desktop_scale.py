"""Execute the shipping scale policy on the host JDK, without Android or a device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class AndroidDesktopScaleTest(unittest.TestCase):
    def test_shipping_policy(self):
        source = ROOT / "native/platform/android/src/org/colosseum/platform/DesktopPresentationScale.java"
        with tempfile.TemporaryDirectory(prefix="desktop-scale-") as directory:
            result = subprocess.run([shutil.which("javac"), "-d", directory, str(source),
                str(ROOT / "tests/android/DesktopPresentationScaleTest.java")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([shutil.which("java"), "-cp", directory,
                "org.colosseum.platform.DesktopPresentationScaleTest"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("DESKTOP_PRESENTATION_SCALE_OK", result.stdout)

    def test_normal_launcher_and_compiled_pre_qt_hook(self):
        import os
        sdk = Path(os.environ["ANDROID_SDK_ROOT"])
        qt = Path(os.environ["QT_ANDROID_ROOT"])
        java = ROOT / "native/platform/android/src/org/colosseum/platform"
        manifest = ET.parse(ROOT / "native/platform/android/AndroidManifest.xml")
        ns = "{http://schemas.android.com/apk/res/android}"
        activity = manifest.find("./application/activity")
        self.assertEqual(activity.get(ns + "screenOrientation"), "sensorLandscape")
        compatibility = activity.find("./property[@" + ns + "name='android.window.PROPERTY_COMPAT_ALLOW_RESTRICTED_RESIZABILITY']")
        self.assertIsNotNone(compatibility)
        self.assertEqual(compatibility.get(ns + "value"), "true")
        self.assertEqual(activity.find("./intent-filter/action").get(ns + "name"), "android.intent.action.MAIN")
        self.assertEqual(activity.find("./intent-filter/category").get(ns + "name"), "android.intent.category.LAUNCHER")
        with tempfile.TemporaryDirectory(prefix="desktop-activity-api-") as directory:
            work = Path(directory)
            # Unrelated service/redraw implementations have their own Task 2 tests;
            # these declarations isolate compilation of the real Activity/API calls.
            stubs = work / "ActivityDependencies.java"
            stubs.write_text('''package org.colosseum.platform;
class DownloadService {
    static void handleIntent(android.content.Intent intent) {}
    static void permissionResult(int request, int[] result) {}
}
class QtSurfaceRedrawGate {
    QtSurfaceRedrawGate(android.content.Context context) {}
    void setBridgeHandle(long handle) {}
    void framePresented(long token) {}
    void dispose() {}
}
''', encoding="utf-8")
            classpath = os.pathsep.join(map(str, [sdk / "platforms/android-36/android.jar", qt / "jar/Qt6Android.jar"]))
            result = subprocess.run([shutil.which("javac"), "--release", "17", "-cp", classpath,
                "-d", directory, str(stubs), str(java / "ColosseumActivity.java"),
                str(java / "DesktopPresentationScale.java"), str(java / "PlayerOrientationRequest.java"),
                str(qt / "src/android/java/src/org/qtproject/qt/android/bindings/QtActivity.java")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            bytecode = subprocess.check_output([shutil.which("javap"), "-cp", directory, "-c", "-p",
                "org.colosseum.platform.ColosseumActivity"], text=True)
            self.assertIn("DesktopPresentationScale.defaultVisualZoom", bytecode)
            on_create = bytecode.split("public void onCreate(android.os.Bundle);", 1)[1].split("private void configureDesktopScale", 1)[0]
            self.assertLess(on_create.index("Method configureDesktopScale"), on_create.index("QtActivity.onCreate"))
            # Qt's fullscreen children retain the full window bounds when an outer
            # content view is padded. The shipping Activity must leave insets and
            # content geometry to Qt rather than offsetting only QtRootLayout.
            for method in ("setPadding", "setOnApplyWindowInsetsListener", "requestApplyInsets"):
                with self.subTest(forbidden_outer_geometry_call=method):
                    self.assertFalse("android/view/View." + method + ":" in bytecode,
                                     "Activity intercepts Qt content geometry via " + method)


if __name__ == "__main__":
    unittest.main()

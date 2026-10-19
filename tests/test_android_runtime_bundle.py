"""Exercise the real Android packaging CMake without an Android/Qt toolchain."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verify_android_apk_runtime_bundle import (
    QML_PREFIX, RUNTIME_PREFIX, qml_policy_failures, runtime_manifest_failures,
    android_java_policy_failures,
)

ROOT = Path(__file__).resolve().parents[1]


def stage_runtime(directory: Path) -> Path:
    source = directory / "driver"
    build = directory / "build"
    source.mkdir(parents=True, exist_ok=True)
    tls = directory / "openssl/lib"
    tls.mkdir(parents=True, exist_ok=True)
    for name in ("libcrypto_3.so", "libssl_3.so"):
        (tls / name).touch()
    (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.21)
project(AndroidRuntimeStaging VERSION 1.0 LANGUAGES NONE)
add_custom_target(colosseum)
set(CMAKE_CURRENT_SOURCE_DIR "{(ROOT / 'native').as_posix()}")
set(OPENSSL_ROOT_DIR "{(directory / 'openssl').as_posix()}")
include("{(ROOT / 'native/cmake/android.cmake').as_posix()}")
set_property(TARGET colosseum PROPERTY SOURCES "")
''')
    result = subprocess.run(["cmake", "-S", str(source), "-B", str(build)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return build / "android-package/assets/colosseum-runtime"


def runtime_files(runtime: Path) -> dict[str, bytes]:
    return {RUNTIME_PREFIX + path.relative_to(runtime).as_posix(): path.read_bytes()
            for path in sorted(runtime.rglob("*")) if path.is_file()}


@unittest.skipUnless(shutil.which("cmake"), "CMake required for real staging test")
class AndroidRuntimeBundleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="android-runtime-test-")
        cls.runtime = stage_runtime(Path(cls.directory.name))
        cls.files = runtime_files(cls.runtime)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_actual_staged_sources_exclude_desktop_implementations(self):
        self.assertEqual([], qml_policy_failures(self.files))
        self.assertTrue((self.runtime / "qml/Main.qml").is_file())
        self.assertTrue((self.runtime / "qml/reader2/AndroidPaper.qml").is_file())
        self.assertEqual((ROOT / "qml/Main.qml").read_bytes(),
                         (self.runtime / "qml/Main.qml").read_bytes())

    def test_coming_soon_desktop_worlds_are_absent(self):
        self.assertFalse((self.runtime / "qml/vinyl").exists())
        self.assertFalse((self.runtime / "qml/feria").exists())

    def test_staged_manifests_match_exact_file_set_and_bytes(self):
        self.assertEqual([], runtime_manifest_failures(self.files))
        extra = dict(self.files)
        extra[QML_PREFIX + "Unexpected.qml"] = b"import QtQuick\nItem {}"
        self.assertIn("RUNTIME_MANIFEST_MISMATCH qml/Unexpected.qml",
                      runtime_manifest_failures(extra))

    def test_java_staging_keeps_neutral_admission_and_excludes_vault(self):
        package = self.runtime.parent.parent
        self.assertFalse((package / "src/org/colosseum/vault").exists())
        self.assertTrue((package / "src/org/colosseum/media/MediaAdmissionProbe.java").is_file())
        for path in package.rglob("*.java"):
            with self.subTest(source=str(path.relative_to(package))):
                self.assertNotIn("org.colosseum.vault", path.read_text())
        self.assertNotIn("org.colosseum.vault", (package / "AndroidManifest.xml").read_text())

    def test_restage_is_deterministic(self):
        again = stage_runtime(Path(self.directory.name))
        self.assertEqual(self.files, runtime_files(again))

    def test_selector_relative_paths_preserved_without_desktop_base_code(self):
        for selector in (ROOT / "qml").rglob("+android/*.qml"):
            if selector.name.startswith("Vault"):
                continue
            relative = selector.relative_to(ROOT / "qml")
            with self.subTest(selector=str(relative)):
                self.assertEqual(selector.read_bytes(), (self.runtime / "qml" / relative).read_bytes())
                base = relative.parent.parent / relative.name
                self.assertIn("Android selector discovery placeholder",
                              (self.runtime / "qml" / base).read_text())


class AndroidRuntimePolicyTest(unittest.TestCase):
    def test_dex_vault_class_and_reference_are_rejected(self):
        for name in ("classes.dex", "classes2.dex", "Other.class"):
            with self.subTest(name=name):
                self.assertTrue(android_java_policy_failures({name: b"Lorg/colosseum/vault/VaultMediaProbe;"}))
        self.assertEqual([], android_java_policy_failures({
            "classes.dex": b"Lorg/colosseum/media/MediaAdmissionProbe;"}))

    def test_vinyl_sources_and_imports_are_rejected(self):
        files = {QML_PREFIX + "vinyl/World.qml": b"import QtQuick\nItem {}"}
        self.assertIn("FORBIDDEN_QML_FILE vinyl/World.qml", qml_policy_failures(files))
        files[QML_PREFIX + "World.qml"] = b'import "vinyl" as Vinyl\nItem {}'
        self.assertIn("FORBIDDEN_DESKTOP_IMPORT World.qml", qml_policy_failures(files))

    def test_missing_quoted_javascript_import_is_rejected(self):
        files = {QML_PREFIX + "World.qml": b'import "Missing.js" as Api\nItem {}'}
        self.assertEqual(["MISSING_RELATIVE_IMPORT World.qml: Missing.js"], qml_policy_failures(files))
        files[QML_PREFIX + "Missing.js"] = b".pragma library"
        self.assertEqual([], qml_policy_failures(files))

    def test_selector_relative_directory_import_is_resolved(self):
        files = {QML_PREFIX + "+android/Widget.qml": b'import ".."\nItem {}',
                 QML_PREFIX + "Theme.qml": b"import QtQuick\nQtObject {}"}
        self.assertEqual([], qml_policy_failures(files))


    def test_detector_rejects_inactive_webengine_and_world_sources(self):
        samples = {
            "Unreachable.qml": b"import QtQuick\nimport QtWebEngine 1.10\nItem {}",
            "feria/Unused.qml": b"import QtQuick\nItem {}",
            "+android/VaultPage.qml": b"import QtQuick\nItem {}",
            "Unused.qml": b'import "feria" as Desktop\nItem {}',
        }
        for path, data in samples.items():
            with self.subTest(path=path):
                self.assertTrue(qml_policy_failures({QML_PREFIX + path: data}))


if __name__ == "__main__":
    unittest.main()

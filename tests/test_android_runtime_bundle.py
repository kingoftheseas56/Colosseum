"""Exercise the real Android packaging CMake without an Android/Qt toolchain."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from verify_android_apk_runtime_bundle import (
    QML_PREFIX, RUNTIME_PREFIX, qml_policy_failures, runtime_manifest_failures,
    android_java_policy_failures,
)

ROOT = Path(__file__).resolve().parents[1]


def stage_runtime(directory: Path, nuvio: bool = True) -> Path:
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
set(COLOSSEUM_NUVIO_ANDROID {"ON" if nuvio else "OFF"})
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

    def test_desktop_composition_selected_with_real_platform_picker(self):
        for name in ("TopBar.qml", "Taskbar.qml", "ExtensionsHouseHero.qml"):
            with self.subTest(component=name):
                self.assertEqual((ROOT / "qml" / name).read_bytes(), (self.runtime / "qml" / name).read_bytes())
                self.assertFalse((self.runtime / "qml/+android" / name).exists())
        for name in ("VaultDoor.qml", "VaultHomeWidget.qml", "VaultLibraryButton.qml",
                     "VaultLibraryPage.qml", "VaultAccessCard.qml", "VaultFolderChooser.qml",
                     "comicreader/VaultComicReader.qml"):
            self.assertEqual((ROOT / "qml" / name).read_bytes(), (self.runtime / "qml" / name).read_bytes())
        # The retired pre-library pages stay out of the APK; nothing instantiates them.
        self.assertFalse((self.runtime / "qml/+android/VaultPage.qml").exists())
        self.assertFalse((self.runtime / "qml/VaultPage.qml").exists())
        self.assertFalse((self.runtime / "qml/+android/LocalIdentityDialog.qml").exists())
        self.assertEqual((ROOT / "qml/LocalIdentityDialog.qml").read_bytes(),
                         (self.runtime / "qml/LocalIdentityDialog.qml").read_bytes())
        self.assertTrue((self.runtime / "qml/reader2/AndroidPaper.qml").is_file())

    def test_coming_soon_desktop_worlds_are_absent(self):
        self.assertFalse((self.runtime / "qml/vinyl").exists())
        self.assertFalse((self.runtime / "qml/feria").exists())

    def test_staged_manifests_match_exact_file_set_and_bytes(self):
        self.assertEqual([], runtime_manifest_failures(self.files))
        extra = dict(self.files)
        extra[QML_PREFIX + "Unexpected.qml"] = b"import QtQuick\nItem {}"
        self.assertIn("RUNTIME_MANIFEST_MISMATCH qml/Unexpected.qml",
                      runtime_manifest_failures(extra))

    def test_java_staging_ships_vault_probe_not_folder_picker_helper(self):
        package = self.runtime.parent.parent
        self.assertTrue((package / "src/org/colosseum/vault/VaultMediaProbe.java").is_file())
        self.assertFalse((package / "src/org/colosseum/vault/DocumentMetadata.java").exists())
        self.assertTrue((package / "src/org/colosseum/platform/AllFilesAccess.java").is_file())
        self.assertTrue((package / "src/org/colosseum/platform/StorageVolumes.java").is_file())
        self.assertTrue((package / "src/org/colosseum/media/MediaAdmissionProbe.java").is_file())
        for path in package.rglob("*.java"):
            with self.subTest(source=str(path.relative_to(package))):
                self.assertNotIn("DocumentMetadata", path.read_text())
        manifest = (package / "AndroidManifest.xml").read_text()
        self.assertIn("android.permission.MANAGE_EXTERNAL_STORAGE", manifest)
        self.assertIn('android:requestLegacyExternalStorage="true"', manifest)

    def test_restage_is_deterministic(self):
        again = stage_runtime(Path(self.directory.name))
        self.assertEqual(self.files, runtime_files(again))

    def test_nuvio_size_baseline_excludes_only_service_package_code(self):
        enabled = self.runtime.parent.parent
        baseline_runtime = stage_runtime(Path(self.directory.name) / "nuvio-off", nuvio=False)
        baseline = baseline_runtime.parent.parent
        self.assertEqual(self.files, runtime_files(baseline_runtime))
        self.assertTrue((enabled / "src/org/colosseum/nuvio/NuvioService.java").is_file())
        self.assertTrue((enabled / "src/org/colosseum/nuvio/NuvioProcessBridge.java").is_file())
        self.assertFalse((baseline / "src/org/colosseum/nuvio").exists())
        enabled_manifest = (enabled / "AndroidManifest.xml").read_text()
        baseline_manifest = (baseline / "AndroidManifest.xml").read_text()
        android = "{http://schemas.android.com/apk/res/android}"
        slots = [(service.get(android + "process"), service.get(android + "exported"))
                 for service in ET.fromstring(enabled_manifest).findall("application/service")
                 if (service.get(android + "process") or "").startswith(":nuvio")]
        self.assertCountEqual([(":nuvio0", "false"), (":nuvio1", "false")], slots)
        self.assertNotIn("org.colosseum.nuvio", baseline_manifest)
        self.assertNotIn("colosseum_nuvio_host", baseline_manifest)

    def test_selector_relative_paths_preserved_without_desktop_base_code(self):
        for selector in (ROOT / "qml").rglob("+android/*.qml"):
            if selector.name.startswith("Vault") or selector.name in {"TopBar.qml", "Taskbar.qml", "ExtensionsHouseHero.qml"}:
                continue
            relative = selector.relative_to(ROOT / "qml")
            with self.subTest(selector=str(relative)):
                self.assertEqual(selector.read_bytes(), (self.runtime / "qml" / relative).read_bytes())
                base = relative.parent.parent / relative.name
                self.assertIn("Android selector discovery placeholder",
                              (self.runtime / "qml" / base).read_text())


class AndroidRuntimePolicyTest(unittest.TestCase):
    def test_dex_folder_picker_helper_is_rejected_but_vault_probe_ships(self):
        for name in ("classes.dex", "classes2.dex", "Other.class"):
            with self.subTest(name=name):
                self.assertTrue(android_java_policy_failures({name: b"Lorg/colosseum/vault/DocumentMetadata;"}))
                self.assertEqual([], android_java_policy_failures({name: b"Lorg/colosseum/vault/VaultMediaProbe;"}))
        self.assertEqual([], android_java_policy_failures({
            "classes.dex": b"Lorg/colosseum/media/MediaAdmissionProbe;"}))

    def test_vinyl_sources_and_imports_are_rejected(self):
        files = {QML_PREFIX + "vinyl/World.qml": b"import QtQuick\nItem {}"}
        self.assertIn("FORBIDDEN_QML_FILE vinyl/World.qml", qml_policy_failures(files))
        files[QML_PREFIX + "World.qml"] = b'import "vinyl" as Vinyl\nItem {}'
        self.assertIn("FORBIDDEN_DESKTOP_IMPORT World.qml", qml_policy_failures(files))

    def test_vault_world_ships_but_retired_pages_do_not(self):
        for name in ("VaultDoor.qml", "VaultLibraryPage.qml", "VaultAccessCard.qml",
                     "VaultFolderChooser.qml", "comicreader/VaultComicReader.qml"):
            self.assertEqual([], qml_policy_failures({QML_PREFIX + name: b"import QtQuick" + bytes([10]) + b"Item {}"}))
        for name in ("+android/VaultPage.qml", "VaultPage.qml"):
            self.assertTrue(qml_policy_failures({QML_PREFIX + name: b"import QtQuick" + bytes([10]) + b"Item {}"}))

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

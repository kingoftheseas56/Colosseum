from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "native" / "CMakeLists.txt"


def cmake_text() -> str:
    return CMAKE.read_text(encoding="utf-8")


def app_source_block(text: str) -> str:
    match = re.search(
        r"qt_add_executable\(colosseum\s*(.*?)\n\)",
        text,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("colosseum must be declared with qt_add_executable")
    return match.group(1)


class AndroidBuildGraphContract(unittest.TestCase):
    def test_shipping_target_uses_qt_android_aware_executable(self):
        text = cmake_text()
        self.assertIn("qt_add_executable(colosseum", text)
        self.assertNotRegex(text, r"(?m)^add_executable\(colosseum\b")

    def test_android_shared_qt_components_exclude_desktop_modules(self):
        text = cmake_text()
        self.assertIn(
            "find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui Sql WebSockets Concurrent)",
            text,
        )
        desktop_modules = re.search(
            r"if\(NOT ANDROID\).*?find_package\(Qt6 REQUIRED COMPONENTS WebEngineQuick WebChannel\).*?endif\(\)",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(desktop_modules, "WebEngine/WebChannel must be desktop-only")

    def test_mpv_discovery_is_desktop_only(self):
        text = cmake_text()
        desktop_mpv = re.search(
            r"if\(NOT ANDROID\).*?find_package\(MpvQt REQUIRED\).*?endif\(\)",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(desktop_mpv, "MpvQt/libmpv discovery must not run for Android")

    def test_android_shipping_sources_exclude_desktop_organs(self):
        sources = app_source_block(cmake_text())
        forbidden = (
            "account/WindowsAccountCredentialStore.cpp",
            "account/WindowsAccountSensitiveClipboard.cpp",
            "update/",
            "installed_chronicle.qrc",
            "player/MediaAdmissionProbe.cpp",
            "player/livestore.cpp",
            "player/mpvitem.cpp",
            "player/seekthumbnailer.cpp",
            "player/powerstore.cpp",
            "player/streamserver.cpp",
        )
        for token in forbidden:
            with self.subTest(token=token):
                self.assertNotIn(token, sources)

    def test_desktop_organs_are_preserved_behind_platform_guard(self):
        text = cmake_text()
        guarded = re.search(
            r"if\(NOT ANDROID\)\s*# Desktop-only shipping organs.*?target_sources\(colosseum PRIVATE(.*?)\n\s*\)\s*endif\(\)",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(guarded, "desktop sources must be preserved in a NOT ANDROID target_sources block")
        block = guarded.group(1)
        for token in (
            "update/UpdateService.cpp",
            "player/MediaAdmissionProbe.cpp",
            "player/mpvitem.cpp",
            "player/streamserver.cpp",
        ):
            with self.subTest(token=token):
                self.assertIn(token, block)

    def test_android_link_graph_has_no_webengine_or_mpv(self):
        text = cmake_text()
        shared_link = re.search(
            r"target_link_libraries\(colosseum PRIVATE\s*(.*?)\n\)", text, re.DOTALL
        )
        self.assertIsNotNone(shared_link)
        block = shared_link.group(1)
        for token in ("MpvQt::MpvQt", "Qt6::WebEngineQuick", "Qt6::WebChannel", "Libmpv"):
            with self.subTest(token=token):
                self.assertNotIn(token, block)
        self.assertIn("Qt6::WebSockets", block)
        self.assertIn("colosseum_libtorrent", block)

    def test_android_qml_deployment_uses_filtered_staging_root(self):
        text = cmake_text()
        self.assertIn("COLOSSEUM_ANDROID_QML_ROOT", text)
        self.assertIn("QT_QML_ROOT_PATH", text)
        self.assertIn('^reader2/Paper[.]qml$', text)
        self.assertIn('^player2/', text)
        self.assertIn('^player2host/', text)
        self.assertIn('Paper Paper.qml', text)
        self.assertIn('string(REPLACE', text)

    def test_android_target_pins_first_apk_baseline(self):
        text = cmake_text()
        android_block = re.search(
            r"if\(ANDROID\)\s*target_sources\(colosseum PRIVATE(.*?)\nendif\(\)",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(android_block, "Android target composition block must exist")
        block = android_block.group(1)
        for token in (
            'QT_ANDROID_ABIS "arm64-v8a"',
            "QT_ANDROID_MIN_SDK_VERSION 28",
            "QT_ANDROID_COMPILE_SDK_VERSION 36",
            "QT_ANDROID_TARGET_SDK_VERSION 36",
            'QT_ANDROID_APP_NAME "Colosseum"',
            'QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"',
        ):
            with self.subTest(token=token):
                self.assertIn(token, block)

    def test_desktop_harness_estate_is_not_configured_on_android(self):
        text = cmake_text()
        self.assertIn("# Desktop-only harness and Player 2 estate", text)
        harness_guard = re.search(
            r"if\(NOT ANDROID\)\s*# Desktop-only harness and Player 2 estate.*?add_executable\(tankoyomi_provider_registry_harness",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(harness_guard)
        ctest_guard = re.search(
            r"include\(CTest\).*?if\(BUILD_TESTING\).*?add_subdirectory\(.*?endif\(\)\s*endif\(\)\s*# NOT ANDROID: desktop harness/test estate\s*$",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(ctest_guard)


if __name__ == "__main__":
    unittest.main()

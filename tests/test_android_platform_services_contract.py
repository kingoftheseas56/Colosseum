from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "native"


def test_platform_services_are_wired_into_build_graph():
    cmake = (NATIVE / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    required_sources = [
        "account/AccountCredentialStoreFactory.cpp",
        "account/AndroidAccountCredentialStore.cpp",
        "account/AndroidJniSecureStorageBackend.cpp",
        "platform/PlatformKind.cpp",
        "platform/PlatformRuntime.cpp",
        "platform/AndroidWindowModeAdapter.cpp",
        "platform/BackgroundDownloadBridge.cpp",
    ]
    for source in required_sources:
        assert source in cmake, f"missing app build wiring: {source}"
    assert "add_executable(platform_services_harness" in cmake
    assert "QT_ANDROID_PACKAGE_SOURCE_DIR" in cmake
    java = NATIVE / "platform" / "android" / "src" / "org" / "colosseum" / "platform" / "SecureCredentialStore.java"
    assert java.is_file(), "Android secure-store Java source is outside package-source src/"


def test_platform_services_harness_is_registered_with_ctest():
    tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    assert "colosseum_register_harness(platform_services_harness" in tests_cmake


def test_platform_runtime_exposes_android_tv_contract():
    header = (NATIVE / "platform" / "PlatformRuntime.h").read_text(encoding="utf-8-sig")
    source = (NATIVE / "platform" / "PlatformRuntime.cpp").read_text(encoding="utf-8-sig")
    assert "Q_PROPERTY(bool androidTelevision READ androidTelevision CONSTANT)" in header
    assert "bool androidTelevision() const;" in header
    assert "bool Runtime::androidTelevision() const" in source
    assert "QNativeInterface::QAndroidApplication::context()" in source
    assert "getCurrentModeType" in source
    assert "android.software.leanback" in source


if __name__ == "__main__":
    test_platform_services_are_wired_into_build_graph()
    test_platform_services_harness_is_registered_with_ctest()
    test_platform_runtime_exposes_android_tv_contract()
    print("ANDROID_PLATFORM_SERVICES_CONTRACT_OK")

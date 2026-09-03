from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "native"


def test_android_vault_backend_is_complete_and_wired():
    required = [
        NATIVE / "engine" / "VaultLocation.h",
        NATIVE / "engine" / "VaultAndroidIndexAdapter.h",
        NATIVE / "engine" / "VaultAndroidIndexAdapter.cpp",
        NATIVE / "engine" / "VaultAndroidStorageBridge.h",
        NATIVE / "engine" / "VaultAndroidStorageBridge.cpp",
    ]
    for path in required:
        assert path.is_file(), f"missing Android Vault seam: {path.name}"

    native_cmake = (NATIVE / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    for source in ["VaultAndroidIndexAdapter.cpp", "VaultAndroidStorageBridge.cpp"]:
        assert source in native_cmake, f"missing app wiring: {source}"

    tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    assert "add_executable(tst_vault_android_index_adapter" in tests_cmake
    assert "colosseum.qttest.vault_android_index_adapter" in tests_cmake


def test_android_vault_runtime_avoids_desktop_watcher_and_ffprobe_path():
    library = (NATIVE / "engine" / "VaultLibrary.cpp").read_text(encoding="utf-8-sig")
    assert "#ifndef Q_OS_ANDROID" in library
    assert "m_watcher = new VaultWatcher" in library
    adapter = (NATIVE / "engine" / "VaultAndroidIndexAdapter.cpp").read_text(encoding="utf-8-sig")
    assert "QDirIterator" not in adapter
    assert "ffprobe" not in adapter.lower()


if __name__ == "__main__":
    test_android_vault_backend_is_complete_and_wired()
    test_android_vault_runtime_avoids_desktop_watcher_and_ffprobe_path()
    print("ANDROID_VAULT_CONTRACT_OK")

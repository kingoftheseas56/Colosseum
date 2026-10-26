"""Android excludes the Vault world; desktop keeps its native implementation.

Evaluates the actual CMake source declarations without requiring an Android SDK,
then preprocesses the native startup/bridge gates without Qt headers. Run directly
or with pytest; CMAKE_COMMAND and CXX can select the installed tools.
"""
from functools import lru_cache
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "native"


def _tool(variable, default):
    value = os.environ.get(variable, default)
    resolved = shutil.which(value)
    assert resolved, f"{value} is required; install it or set {variable}"
    return resolved


@lru_cache(maxsize=2)
def _application_sources(android):
    cmake = (NATIVE / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    start = cmake.index("qt_add_executable(colosseum")
    end = cmake.index('if(ANDROID)\n    include(', start)
    declarations = cmake[start:end]
    android_cmake = (NATIVE / "cmake" / "android.cmake").read_text(encoding="utf-8-sig")
    start = android_cmake.index("target_sources(colosseum")
    end = android_cmake.index("\n)", start) + 2
    declarations += "\nif(ANDROID)\n" + android_cmake[start:end] + "\nendif()\n"
    # CMake evaluates the platform conditions and the target-aware source macros
    # collect only colosseum's graph. The small library/link declarations added
    # around the executable are inert here; no configure/package step runs.
    script = """macro(qt_add_executable name)
if("${name}" STREQUAL "colosseum")
  set(colosseum_sources ${ARGN})
endif()
endmacro()
macro(target_sources target visibility)
if("${target}" STREQUAL "colosseum")
  list(APPEND colosseum_sources ${ARGN})
endif()
endmacro()
macro(add_library target)
endmacro()
macro(target_link_libraries target)
endmacro()
macro(target_include_directories target)
endmacro()
""" + declarations + '\nfile(WRITE "${OUTPUT}" "${colosseum_sources}")\n'
    with tempfile.TemporaryDirectory(prefix="colosseum-vault-contract-") as directory:
        script_path = Path(directory) / "sources.cmake"
        output_path = Path(directory) / "sources.txt"
        script_path.write_text(script, encoding="utf-8")
        result = subprocess.run(
            [_tool("CMAKE_COMMAND", "cmake"), f"-DANDROID={'TRUE' if android else 'FALSE'}",
             f"-DOUTPUT={output_path.as_posix()}", "-P", str(script_path)],
            capture_output=True, text=True,
        )
        if result.returncode:
            raise AssertionError(
                f"CMake source evaluation failed ({result.returncode}):\n{result.stderr}\n{result.stdout}"
            )
        return set(output_path.read_text(encoding="utf-8").split(";"))


def test_android_excludes_vault_native_sources():
    sources = _application_sources(True)
    forbidden = sorted(source for source in sources
                       if source.startswith(("engine/Vault", "engine/LocalLaunch", "feria/")))
    assert not forbidden, f"desktop-only product code in Android target: {forbidden}"
    # These similarly named services belong to catalog downloads and credentials,
    # not the Vault world, and remain required on Android.
    assert {"engine/CatalogVaultClient.cpp", "trackers/TrackerCredentialVault.cpp",
            "engine/MalCatalog.cpp", "engine/ImdbCatalog.cpp"} <= sources


def test_desktop_keeps_vault_native_sources():
    sources = _application_sources(False)
    assert {"engine/VaultLibrary.cpp", "engine/VaultKit.cpp", "engine/VaultIndex.cpp",
            "engine/VaultScanner.cpp", "engine/VaultWatcher.cpp", "engine/VaultEnricher.cpp",
            "engine/VaultForensics.cpp", "engine/VaultBookCoverProvider.cpp",
            "engine/VaultThumbnailer.cpp", "engine/LocalLaunch.cpp"} <= sources


def _active_source(relative, android):
    source = (NATIVE / relative).read_text(encoding="utf-8-sig")
    # Evaluate only this file's platform branches, not Qt's headers/platform ABI.
    source = re.sub(r"^\s*#\s*include[^\n]*", "", source, flags=re.MULTILINE)
    command = [_tool("CXX", "c++"), "-E", "-P", "-x", "c++"]
    if android:
        command += ["-DQ_OS_ANDROID"]
    return subprocess.run(command + ["-"], input=source, capture_output=True,
                          text=True, encoding="utf-8", check=True).stdout


def test_android_has_no_vault_startup_or_bridge():
    for relative in ("main.cpp", "devtools/LanistaServer.cpp", "devtools/LanistaServer.h"):
        active = _active_source(relative, True)
        assert not re.search(r"\b(?:Vault[A-Z]\w*|vault[A-Z]\w*|LocalLaunch|localLaunch)\b", active), relative
        assert '"vault-forensics"' not in active, relative
    main = _active_source("main.cpp", True)
    assert '"CatalogVault"' in main
    assert '"[boot] Main.qml loaded"' in main
    desktop = _active_source("main.cpp", False)
    assert 'new VaultLibrary(' in desktop and 'new LocalLaunch(' in desktop


def test_catalog_title_matching_is_independent_of_vault():
    for relative in ("engine/MalCatalog.cpp", "engine/ImdbCatalog.cpp"):
        source = (NATIVE / relative).read_text(encoding="utf-8-sig")
        assert '"VaultKit.h"' not in source
        assert "VaultKit::" not in source
        assert "MediaTitleNormalization::normalizedTitle" in source
    # Desktop's historical API still uses the very same matching algorithm.
    source = (NATIVE / "engine" / "VaultKit.cpp").read_text(encoding="utf-8-sig")
    assert "return MediaTitleNormalization::normalizedTitle(rawTitle);" in source


if __name__ == "__main__":
    test_android_excludes_vault_native_sources()
    test_desktop_keeps_vault_native_sources()
    test_android_has_no_vault_startup_or_bridge()
    test_catalog_title_matching_is_independent_of_vault()
    print("ANDROID_VAULT_CONTRACT_OK")

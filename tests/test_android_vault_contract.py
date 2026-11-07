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

_SHARED_AUDIOBOOK_SOURCES = {
    "engine/AudiobookDownloader.cpp",
    "engine/AudiobookDownloader.h",
    "engine/AudiobookTorrentTransport.cpp",
    "engine/AudiobookTorrentTransport.h",
    "engine/AudiobookTorrentEngineAdapter.h",
}


def _tool(variable, default):
    value = os.environ.get(variable, default)
    resolved = shutil.which(value)
    assert resolved, f"{value} is required; install it or set {variable}"
    return resolved


def _extract_cmake_command(text, start):
    opening = text.index("(", start)
    depth = 0
    quoted = False
    escaped = False
    comment = False
    for index in range(opening, len(text)):
        char = text[index]
        if comment:
            if char == "\n":
                comment = False
        elif quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == "#":
            comment = True
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise ValueError(f"unclosed CMake command at offset {start}")


def _conditioned_target_sources(cmake):
    command_start = re.compile(
        r"(?im)^[ \t]*(?P<name>function|macro|endfunction|endmacro|"
        r"if|elseif|else|endif|target_sources)\s*\("
    )
    conditions = []
    declarations = []
    definition_depth = 0
    for match in command_start.finditer(cmake):
        command = _extract_cmake_command(cmake, match.start())
        name = match.group("name").lower()
        arguments = command[command.index("(") + 1:-1].strip()
        if name in ("function", "macro"):
            definition_depth += 1
        elif name in ("endfunction", "endmacro"):
            definition_depth = max(0, definition_depth - 1)
        elif definition_depth:
            continue
        elif name == "if":
            condition = " ".join(arguments.split())
            conditions.append({"branches": [condition], "active": condition})
        elif name == "elseif" and conditions:
            condition = " ".join(arguments.split())
            frame = conditions[-1]
            previous = " OR ".join(f"({branch})" for branch in frame["branches"])
            frame["active"] = f"NOT ({previous}) AND ({condition})"
            frame["branches"].append(condition)
        elif name == "else" and conditions:
            frame = conditions[-1]
            previous = " OR ".join(f"({branch})" for branch in frame["branches"])
            frame["active"] = f"NOT ({previous})"
        elif name == "endif" and conditions:
            conditions.pop()
        elif name == "target_sources" and re.match(
            r"(?i)^colosseum(?:\s|$)", arguments
        ):
            wrapped = [f"if({frame['active']})" for frame in conditions]
            wrapped.append(command)
            wrapped.extend("endif()" for _ in conditions)
            declarations.append("\n".join(wrapped))
    return declarations


def _source_list_definitions(cmake, executable_start, executable_command):
    names = sorted(set(re.findall(
        r"\$\{\s*([A-Za-z_][A-Za-z0-9_]*_SOURCES)\s*\}", executable_command
    )))
    prefix = cmake[:executable_start]
    definitions = []
    for name in names:
        pattern = re.compile(
            rf"(?im)^[ \t]*set\s*\(\s*{re.escape(name)}\b"
        )
        matches = list(pattern.finditer(prefix))
        assert matches, f"source list {name} is referenced but not defined before colosseum"
        definitions.append(_extract_cmake_command(prefix, matches[-1].start()))
    return definitions


@lru_cache(maxsize=2)
def _application_sources(android):
    cmake = (NATIVE / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    executable_match = re.search(
        r"(?im)^[ \t]*qt_add_executable\s*\(\s*colosseum\b", cmake
    )
    assert executable_match, "colosseum executable source declaration is missing"
    executable = _extract_cmake_command(cmake, executable_match.start())
    source_lists = _source_list_definitions(
        cmake, executable_match.start(), executable
    )
    declarations = source_lists + [executable]
    declarations.extend(_conditioned_target_sources(cmake))
    android_cmake = (NATIVE / "cmake" / "android.cmake").read_text(encoding="utf-8-sig")
    android_declarations = _conditioned_target_sources(android_cmake)
    # CMake itself evaluates platform conditions. Extract only source commands
    # so nearby package, link, and Android packaging setup is not executed here.
    script = '''set(CMAKE_CURRENT_SOURCE_DIR "${NATIVE_SOURCE_DIR}")
macro(qt_add_executable name)
set(sources ${ARGN})
endmacro()
macro(target_sources name visibility)
if("${name}" STREQUAL "colosseum")
list(APPEND sources ${ARGN})
endif()
endmacro()
# Source extraction also crosses supporting target declarations. They do not
# affect the application's selected source list and are not scriptable in -P.
# Keep CMake's platform conditions, lists and variables real; skip only configure
# operations that would otherwise need targets, subprojects, or a Qt SDK.
macro(add_library)
endmacro()
macro(add_subdirectory)
endmacro()
macro(add_dependencies)
endmacro()
macro(target_include_directories)
endmacro()
macro(target_link_libraries)
endmacro()
macro(target_compile_definitions)
endmacro()
macro(set_property)
endmacro()
macro(find_package)
endmacro()
''' + "\n".join(declarations) + (
        "\nif(ANDROID)\n" + "\n".join(android_declarations) + "\nendif()\n"
        + '\nfile(WRITE "${OUTPUT}" "${sources}")\n'
    )
    with tempfile.TemporaryDirectory(prefix="colosseum-vault-contract-") as directory:
        script_path = Path(directory) / "sources.cmake"
        output_path = Path(directory) / "sources.txt"
        script_path.write_text(script, encoding="utf-8")
        result = subprocess.run(
            [_tool("CMAKE_COMMAND", "cmake"), f"-DANDROID={'TRUE' if android else 'FALSE'}",
             f"-DNATIVE_SOURCE_DIR={NATIVE.as_posix()}",
             f"-DOUTPUT={output_path.as_posix()}", "-P", str(script_path)],
            check=False, capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        return set(output_path.read_text(encoding="utf-8").split(";"))


def _assert_shared_audiobook_sources_are_present(sources):
    cmake = (NATIVE / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    if re.search(r"(?im)^\s*set\s*\(\s*COLOSSEUM_AUDIOBOOK_DOWNLOAD_SOURCES\b", cmake):
        missing = sorted(_SHARED_AUDIOBOOK_SOURCES - sources)
        assert not missing, f"shared audiobook source list was omitted: {missing}"


def test_android_excludes_vault_native_sources():
    sources = _application_sources(True)
    _assert_shared_audiobook_sources_are_present(sources)
    forbidden = sorted(source for source in sources
                       if source.startswith(("engine/Vault", "engine/LocalLaunch", "feria/")))
    assert not forbidden, f"desktop-only product code in Android target: {forbidden}"
    # These similarly named services belong to catalog downloads and credentials,
    # not the Vault world, and remain required on Android.
    assert {"engine/CatalogVaultClient.cpp", "trackers/TrackerCredentialVault.cpp",
            "engine/MalCatalog.cpp", "engine/ImdbCatalog.cpp"} <= sources


def test_desktop_keeps_vault_native_sources():
    sources = _application_sources(False)
    _assert_shared_audiobook_sources_are_present(sources)
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
                          text=True, check=True).stdout


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

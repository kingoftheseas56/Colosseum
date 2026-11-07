"""Configure the real Nuvio target graph against explicit Qt package stubs.

Qt's private target is deliberately unavailable until its component is requested.
This checks configure ownership, not C++ compilation or Android runtime behavior.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CMAKE = shutil.which(os.environ.get("CMAKE_COMMAND", "cmake"))


@unittest.skipUnless(CMAKE and shutil.which("c++") and shutil.which("cc"), "CMake and C/C++ compilers required")
class NuvioCmakeTests(unittest.TestCase):
    def configure(self, android: bool, *, app_wiring: bool = False, enabled: bool = True):
        with tempfile.TemporaryDirectory(prefix="nuvio-cmake-") as temporary:
            root = Path(temporary)
            package = root / "qt-stub"
            package.mkdir()
            (package / "Qt6Config.cmake").write_text('''
foreach(component IN LISTS Qt6_FIND_COMPONENTS)
    if(NOT component STREQUAL "CorePrivate")
        message(FATAL_ERROR "Unexpected Qt component: ${component}")
    endif()
    add_library(Qt6::CorePrivate INTERFACE IMPORTED)
    set(Qt6CorePrivate_FOUND TRUE)
    file(WRITE "${CMAKE_BINARY_DIR}/private-component-requested" "CorePrivate")
endforeach()
set(Qt6_FOUND TRUE)
''')
            wiring = 'add_subdirectory("' + (ROOT / "native/nuvio").as_posix() + '" nuvio)\n'
            if app_wiring:
                main = (ROOT / "native/CMakeLists.txt").read_text()
                start = main.index('if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (ANDROID AND COLOSSEUM_NUVIO_ANDROID))')
                end = main.index('\nif(ANDROID)\n    target_sources(colosseum PRIVATE update/UpdateTrust.cpp)', start)
                wiring = main[start:end]
                (root / "nuvio").symlink_to(ROOT / "native/nuvio", target_is_directory=True)
                (root / "engine").symlink_to(ROOT / "native/engine", target_is_directory=True)
                (root / "stub.cpp").write_text("int app_stub;\n")
                wiring = ('set(CMAKE_SYSTEM_NAME Android)\nadd_library(colosseum SHARED stub.cpp)\n'
                          + wiring + '\nget_target_property(app_links colosseum LINK_LIBRARIES)\n'
                          'file(WRITE "${CMAKE_BINARY_DIR}/app-links" "${app_links}")\n')
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(NuvioConfigureContract LANGUAGES C CXX)
foreach(component IN ITEMS Core Network WebSockets)
    add_library(Qt6::${component} INTERFACE IMPORTED)
endforeach()
add_library(colosseum_account_credentials INTERFACE)
set(ANDROID_ABI x86_64)
set(COLOSSEUM_NUVIO_ANDROID_PROOF OFF)
''' + wiring + '''
if(TARGET colosseum-nuvio-host)
    get_target_property(worker_type colosseum-nuvio-host TYPE)
else()
    set(worker_type ABSENT)
endif()
file(WRITE "${CMAKE_BINARY_DIR}/worker-type" "${worker_type}")
''')
            build = root / "build"
            env = dict(os.environ, COLOSSEUM_TMDB_KEY="")
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(build),
                                     f"-DQt6_DIR={package}", f"-DANDROID={'ON' if android else 'OFF'}",
                                     f"-DCOLOSSEUM_NUVIO_ANDROID={'ON' if enabled else 'OFF'}"],
                                    capture_output=True, text=True, env=env, timeout=60)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(android and enabled, (build / "private-component-requested").exists())
            expected = "ABSENT" if not enabled else "SHARED_LIBRARY" if android else "EXECUTABLE"
            self.assertEqual(expected,
                             (build / "worker-type").read_text())
            if app_wiring:
                self.assertEqual(enabled, "colosseum_nuvio" in (build / "app-links").read_text().split(";"))

    def test_android_explicitly_resolves_core_private(self):
        self.configure(True)

    def test_desktop_does_not_request_android_private_module(self):
        self.configure(False)

    def test_android_application_links_enabled_repository_and_resolver(self):
        self.configure(True, app_wiring=True)

    def test_disabled_android_size_baseline_omits_nuvio_targets(self):
        self.configure(True, app_wiring=True, enabled=False)


if __name__ == "__main__":
    unittest.main()

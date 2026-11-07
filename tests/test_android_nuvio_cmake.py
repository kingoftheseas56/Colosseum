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
    def configure(self, android: bool):
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
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(NuvioConfigureContract LANGUAGES C CXX)
foreach(component IN ITEMS Core Network WebSockets)
    add_library(Qt6::${component} INTERFACE IMPORTED)
endforeach()
add_library(colosseum_account_credentials INTERFACE)
set(ANDROID_ABI x86_64)
set(COLOSSEUM_NUVIO_ANDROID_PROOF OFF)
add_subdirectory("''' + (ROOT / "native/nuvio").as_posix() + '''" nuvio)
get_target_property(worker_type colosseum-nuvio-host TYPE)
file(WRITE "${CMAKE_BINARY_DIR}/worker-type" "${worker_type}")
''')
            build = root / "build"
            env = dict(os.environ, COLOSSEUM_TMDB_KEY="")
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(build),
                                     f"-DQt6_DIR={package}", f"-DANDROID={'ON' if android else 'OFF'}"],
                                    capture_output=True, text=True, env=env, timeout=60)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(android, (build / "private-component-requested").exists())
            self.assertEqual("SHARED_LIBRARY" if android else "EXECUTABLE",
                             (build / "worker-type").read_text())

    def test_android_explicitly_resolves_core_private(self):
        self.configure(True)

    def test_desktop_does_not_request_android_private_module(self):
        self.configure(False)


if __name__ == "__main__":
    unittest.main()

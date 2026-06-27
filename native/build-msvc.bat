@echo off
REM ============================================================================
REM  Colosseum  -  MSVC build (Qt 6.11.1 msvc2022_64 + Ninja)
REM
REM  The QtWebEngine path: Colosseum migrated off MinGW to MSVC so it can host
REM  Tankoban 2's foliate EPUB reader (QWebEngineView + QWebChannel), which only
REM  builds under Qt WebEngine = MSVC on Windows.  See the homeward-foundry recap.
REM
REM  Loads the MSVC 2022 toolchain, configures native\build-msvc, builds it.
REM  Output -> build-msvc\colosseum.exe   (run it via Colosseum.bat / dev.bat).
REM ============================================================================
setlocal
cd /d "%~dp0"

REM 1) MSVC 2022 toolchain (cl / link / INCLUDE / LIB) into this shell.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)

set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "NINJA=C:\Qt\Tools\Ninja\ninja.exe"

REM 2) Configure (Release => /MD CRT, matches the Qt msvc kit + release-only MpvQt).
REM    CMakeLists is toolchain-aware: MSVC picks mpvqt-msvc-install + mpv.lib.
"%CMAKE%" -S . -B build-msvc -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" || (echo CONFIGURE FAILED & exit /b 1)

REM 3) Build.
"%CMAKE%" --build build-msvc || (echo BUILD FAILED & exit /b 1)

echo BUILD_OK

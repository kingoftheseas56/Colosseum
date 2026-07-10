@echo off
REM ============================================================================
REM  Colosseum launcher  -  just double-click this to open Colosseum.
REM
REM  It runs the native disk-cache launcher (the "right method"): the already-
REM  compiled colosseum.exe against the LIVE qml/ tree. No build step -- editing
REM  the QML needs no rebuild, and every cover downloads ONCE then loads from
REM  disk forever (QNetworkDiskCache).  Esc / Ctrl+Q quits; the minimize button
REM  sends it to the taskbar.
REM ============================================================================

REM Run from this file's own folder so qml\Main.qml resolves wherever it's launched from.
cd /d "%~dp0"

REM Put Qt's MSVC runtime on PATH so the launcher finds its DLLs (QtWebEngine path).
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;%PATH%"

if not exist "native\build-msvc\colosseum.exe" (
  echo.
  echo   Colosseum's native launcher isn't built yet ^(native\build-msvc\colosseum.exe is missing^).
  echo   Ask a brother to run native\build-msvc.bat once -- after that, this file just works.
  echo.
  pause >nul
  exit /b 1
)

REM Argless = the user lane: the exe self-locates the repo and self-updates (git pull
REM --ff-only, 8s cap) before it boots. Passing a QML path (dev.bat) skips all of that.
"native\build-msvc\colosseum.exe"

REM Only pause if it crashed, so a clean quit closes silently but an error stays readable.
if errorlevel 1 (
  echo.
  echo   Colosseum exited with an error ^(code %errorlevel%^). Press any key to close.
  pause >nul
)

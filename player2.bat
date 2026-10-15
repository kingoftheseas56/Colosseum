@echo off
setlocal
set "ROOT=%~dp0"

REM If Qt or FFmpeg DLLs are not already on PATH, point these variables at their prefixes.
if defined QT_ROOT if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"
if defined FFMPEG_ROOT if exist "%FFMPEG_ROOT%\bin" set "PATH=%FFMPEG_ROOT%\bin;%PATH%"

if not exist "%ROOT%native\build-player2\player2_harness.exe" (
  echo Player 2 laboratory is not built yet.
  pause
  exit /b 1
)

start "Player 2 laboratory" "%ROOT%native\build-player2\player2_harness.exe" --scenario synthetic

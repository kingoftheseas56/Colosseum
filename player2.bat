@echo off
setlocal
set "ROOT=%~dp0"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"
if not exist "%ROOT%native\build-player2\player2_harness.exe" (
  echo Player 2 laboratory is not built yet.
  pause
  exit /b 1
)
start "Player 2 laboratory" "%ROOT%native\build-player2\player2_harness.exe" --scenario synthetic

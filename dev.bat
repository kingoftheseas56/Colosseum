@echo off
setlocal
cd /d "%~dp0"

echo Colosseum - development loop (live QML reload on save)
echo.

REM Override QT_ROOT if Qt is installed outside the standard source-build location.
if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.11.1\msvc2022_64"
if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"

if not exist "native\build-msvc\colosseum.exe" (
  echo Colosseum has not been built yet.
  echo Run native\build-msvc.bat or follow docs\build\windows.md.
  exit /b 1
)

set "COLOSSEUM_DEV=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QML_DISABLE_DISK_CACHE=1"

native\build-msvc\colosseum.exe qml\Main.qml
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" pause
exit /b %EXIT_CODE%

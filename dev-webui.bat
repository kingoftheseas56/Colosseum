@echo off
setlocal
set "WEBUI_ROOT=%~dp0"
set "CANONICAL_ROOT=%WEBUI_ROOT%"

if not exist "%WEBUI_ROOT%native\build-msvc\colosseum.exe" (
  echo Web Colosseum has not been built in this checkout.
  exit /b 1
)

set "COLOSSEUM_DEV=1"
set "COLOSSEUM_WEBUI=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QML_DISABLE_DISK_CACHE=1"

cd /d "%CANONICAL_ROOT%"
"%WEBUI_ROOT%native\build-msvc\colosseum.exe" "%WEBUI_ROOT%qml\Main.qml"
exit /b %ERRORLEVEL%

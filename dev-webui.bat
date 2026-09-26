@echo off
setlocal
set "WEBUI_ROOT=%~dp0"
set "CANONICAL_ROOT=C:\Users\Suprabha\Desktop\Brotherhood\Colosseum"

if not exist "%WEBUI_ROOT%native\build-msvc\colosseum.exe" (
  echo Web Colosseum has not been built in this worktree.
  echo Build native\build-msvc first.
  exit /b 1
)
if not exist "%CANONICAL_ROOT%\data\mal_catalog.db" (
  echo Canonical Colosseum catalogue data was not found at:
  echo   %CANONICAL_ROOT%\data
  exit /b 1
)

set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;C:\tools\mpvqt-feasibility\libmpv-prefix\bin;%PATH%"
set "COLOSSEUM_DEV=1"
set "COLOSSEUM_WEBUI=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QML_DISABLE_DISK_CACHE=1"
set "COLOSSEUM_APPDATA_TAG="

rem Deliberately run from the canonical repo root so the existing backend
rem resolves the real dev data/*.db catalogues. The executable and QML still
rem come from this isolated Arc 54 worktree. Normal AppData is intentional:
rem WebUI is a presentation layer over the same Colosseum backend/profile.
cd /d "%CANONICAL_ROOT%"
"%WEBUI_ROOT%native\build-msvc\colosseum.exe" "%WEBUI_ROOT%qml\Main.qml"
exit /b %ERRORLEVEL%

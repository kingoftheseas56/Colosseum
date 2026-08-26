@echo off
setlocal
cd /d "%~dp0"

REM Run an existing source-tree build. Override QT_ROOT if Qt is installed elsewhere.
if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.11.1\msvc2022_64"
if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"

if not exist "native\build-msvc\colosseum.exe" (
  echo.
  echo   Colosseum has not been built yet.
  echo   Run native\build-msvc.bat or follow docs\build\windows.md.
  echo.
  pause >nul
  exit /b 1
)

"native\build-msvc\colosseum.exe"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
  echo.
  echo   Colosseum exited with error code %EXIT_CODE%. Press any key to close.
  pause >nul
)

exit /b %EXIT_CODE%

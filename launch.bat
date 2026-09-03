@echo off
setlocal EnableExtensions

cd /d "%~dp0"
set "REPO_ROOT=%~dp0"
set "BUILD_DIR=%REPO_ROOT%native\build-msvc"
set "APP=%BUILD_DIR%\colosseum.exe"

if not exist "%REPO_ROOT%AGENTS.md" (
  echo.
  echo   This is not the Brotherhood Colosseum checkout.
  echo   Put launch.bat in the repository root and try again.
  echo.
  pause
  exit /b 1
)

for /f "delims=" %%B in ('git branch --show-current 2^>nul') do set "BRANCH=%%B"
if not "%BRANCH%"=="master" (
  echo.
  echo   Wrong checkout branch: "%BRANCH%"
  echo   The normal launcher only runs the local master checkout.
  echo.
  pause
  exit /b 1
)

for /f "delims=" %%H in ('git rev-parse --short HEAD 2^>nul') do set "COMMIT=%%H"
echo Colosseum - local master
echo Checkout: %REPO_ROOT%
echo Commit:   %COMMIT%
echo.

if not defined COLOSSEUM_ACCOUNT_SERVICE_URL set "COLOSSEUM_ACCOUNT_SERVICE_URL=https://colosseum-account-service.onrender.com"

if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.11.1\msvc2022_64"
if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"

set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"

if not exist "%VSDEVCMD%" (
  echo.
  echo   Visual Studio 2022 build tools were not found.
  echo   Install the C++ build tools or set up the developer command prompt first.
  echo.
  pause
  exit /b 1
)

echo Building the current local master checkout...
call "%VSDEVCMD%" -arch=x64
if errorlevel 1 (
  echo.
  echo   Visual Studio could not initialize the build environment.
  echo.
  pause
  exit /b 1
)

REM VsDevCmd can rebuild PATH for the developer shell. Restore the Qt and
REM build-directory runtime paths after that call so the freshly built app
REM can locate its Qt, mpv, and companion DLLs when launched from the service.
if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"
if exist "%BUILD_DIR%" set "PATH=%BUILD_DIR%;%PATH%"

cmake --build "%BUILD_DIR%" --target colosseum --parallel 4
if errorlevel 1 (
  echo.
  echo   The current master checkout did not build successfully.
  echo.
  pause
  exit /b 1
)

if not exist "%APP%" (
  echo.
  echo   The build finished without producing colosseum.exe.
  echo.
  pause
  exit /b 1
)

echo.
echo Starting the current local master build...
call :run_app
if "%EXIT_CODE%"=="-1073741819" (
  echo.
  echo   Windows reported an access violation in the generated build.
  echo   Rebuilding generated files cleanly and retrying once...
  cmake --build "%BUILD_DIR%" --target colosseum --clean-first --parallel 4
  if errorlevel 1 (
    echo.
    echo   The clean repair build failed.
    set "EXIT_CODE=1"
    goto :finish
  )
  call :run_app
)

:finish
if not "%EXIT_CODE%"=="0" (
  echo.
  echo   Colosseum exited with error code %EXIT_CODE%.
  pause
)

exit /b %EXIT_CODE%

:run_app
"%APP%"
set "EXIT_CODE=%ERRORLEVEL%"
exit /b 0

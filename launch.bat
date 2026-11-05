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

set "FAST_REQUESTED="
if /I "%~1"=="--fast" set "FAST_REQUESTED=1"
if not "%~1"=="" if /I not "%~1"=="--build" if not defined FAST_REQUESTED goto :usage

if not defined COLOSSEUM_ACCOUNT_SERVICE_URL set "COLOSSEUM_ACCOUNT_SERVICE_URL=https://colosseum-account-service.onrender.com"
set "QT_FORCE_STDERR_LOGGING=1"
set "APP_LOG=%APPDATA%\Brotherhood\Colosseum\logs\colosseum.log"

if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.11.1\msvc2022_64"
if exist "%QT_ROOT%\bin" set "PATH=%QT_ROOT%\bin;%PATH%"
if exist "%BUILD_DIR%" set "PATH=%BUILD_DIR%;%PATH%"
if exist "C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin" set "PATH=C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;%PATH%"
if exist "C:\tools\mpvqt-feasibility\libmpv-prefix\bin" set "PATH=C:\tools\mpvqt-feasibility\libmpv-prefix\bin;%PATH%"
if exist "C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin" set "PATH=C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"

set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake.exe"
if defined FAST_REQUESTED goto :fast_launch

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
if exist "C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin" set "PATH=C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;%PATH%"
if exist "C:\tools\mpvqt-feasibility\libmpv-prefix\bin" set "PATH=C:\tools\mpvqt-feasibility\libmpv-prefix\bin;%PATH%"
if exist "C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin" set "PATH=C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"

"%CMAKE_EXE%" --build "%BUILD_DIR%" --target colosseum_runtime_ready --parallel 4
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

:start
echo.
echo Launching Colosseum...
call :run_app
if defined FAST_REQUESTED goto :finish
if "%EXIT_CODE%"=="-1073741819" (
  echo.
  echo   Windows reported an access violation in the generated build.
  echo   Rebuilding generated files cleanly and retrying once...
  "%CMAKE_EXE%" --build "%BUILD_DIR%" --target colosseum_runtime_ready --clean-first --parallel 4
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
  echo   Application log: %APP_LOG%
  pause
)

exit /b %EXIT_CODE%

:fast_launch
if not exist "%APP%" (
  echo.
  echo   No compiled Colosseum executable was found.
  echo   Run launch.bat --build to compile and launch it.
  echo.
  pause
  exit /b 1
)

REM No MSVC environment, Ninja, or linker is needed for a cached native launch.
REM Verify the exact source QML fingerprint without changing the build manifest.
echo Verifying QML against the last successful build...
"%CMAKE_EXE%" "-DQML_ROOT=%REPO_ROOT%qml" "-DOUTPUT_FILE=%BUILD_DIR%\qml-build.manifest" -DVERIFY_ONLY=ON -P "%REPO_ROOT%native\bootstrap\write_qml_build_manifest.cmake"
if errorlevel 1 (
  echo.
  echo   This source tree no longer matches the built QML manifest.
  echo   Run launch.bat --build to rebuild and refresh it.
  echo.
  pause
  exit /b 1
)
echo Fast launch: using the last compiled native executable.
goto :start

:usage
echo Usage: launch.bat [--build ^| --fast]
echo   launch.bat         - update the local master build, then launch
echo   launch.bat --build - same as launch.bat
echo   launch.bat --fast  - launch the cached app, only if QML matches
exit /b 2

:run_app
"%APP%"
set "EXIT_CODE=%ERRORLEVEL%"
exit /b 0

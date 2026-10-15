@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo FAIL: existing MSVC vcvars64.bat not found
  exit /b 2
)
if not exist "%ROOT%build" mkdir "%ROOT%build"
if errorlevel 1 exit /b 2
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
pushd "%ROOT%"
cl.exe /nologo /std:c++17 /EHsc /W4 /WX /c "ArtistArtworkResolver.cpp" /Fo"build\resolver.obj"
if errorlevel 1 (popd & exit /b 1)
cl.exe /nologo /std:c++17 /EHsc /W4 /WX /c "test_artist_artwork.cpp" /Fo"build\test.obj"
if errorlevel 1 (popd & exit /b 1)
link.exe /nologo /OUT:"build\test_artist_artwork.exe" "build\resolver.obj" "build\test.obj"
if errorlevel 1 (popd & exit /b 1)
"build\test_artist_artwork.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd build-msvc
for %%f in (colosseum.exe Qt6Core.dll Qt6Gui.dll MpvQt.dll libmpv-2.dll) do (
  echo ===%%f===
  dumpbin /dependents "%%f" 2>nul | findstr /i "vsscript"
)

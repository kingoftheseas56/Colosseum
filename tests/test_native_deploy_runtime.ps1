$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw

if ($cmake -notlike '*Qt6Sql.dll*') {
    throw "Native deployment must copy Qt6Sql.dll so colosseum.exe launches without a developer PATH."
}

# --- standalone exe + self-updating launch (spec 2026-07-10) ---
$mainCpp = Get-Content (Join-Path $root "native/main.cpp") -Raw
if ($mainCpp -notlike '*self-update*') {
    throw "Argless launch must self-update (git pull --ff-only) before the engine loads."
}
if ($mainCpp -notlike '*applicationDirPath*') {
    throw "Argless launch must self-locate the repo from the exe position."
}
$bat = Get-Content (Join-Path $root "Colosseum.bat") -Raw
if ($bat -like '*colosseum.exe" "qml*') {
    throw "Colosseum.bat must launch argless (the user lane) - the exe self-locates now."
}
if (!(Test-Path (Join-Path $root "native/deploy-runtime.bat"))) {
    throw "MISSING: native/deploy-runtime.bat (the standalone-runtime deploy)."
}

Write-Host "Native runtime deployment checks passed."

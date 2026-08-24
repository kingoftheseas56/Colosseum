$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$mainCpp = Get-Content (Join-Path $root "native/main.cpp") -Raw
$layoutCpp = Get-Content (Join-Path $root "native/bootstrap/StartupLayout.cpp") -Raw
$package = Get-Content (Join-Path $root "scripts/installer/package_release.sh") -Raw

if ($cmake -notlike '*Qt6Sql.dll*') {
    throw "Native deployment must copy Qt6Sql.dll so colosseum.exe launches without a developer PATH."
}
if ($mainCpp -match 'git\s+pull|pull\s+--ff-only') {
    throw "Startup must never mutate or fast-forward the live source tree."
}
if ($mainCpp -notlike '*resolveStartupLayout*') {
    throw "main.cpp must resolve the build-aligned startup layout."
}
if ($layoutCpp -notlike '*qml-build.manifest*' -or $layoutCpp -notlike '*qml_build_mismatch*') {
    throw "StartupLayout must enforce the QML build manifest and fail closed on mismatch."
}
if ($cmake -notlike '*write_qml_build_manifest.cmake*') {
    throw "The native target must emit qml-build.manifest after every successful build."
}
if ($package -notlike '*qml-build.manifest*' -or $package -notlike '*cmp -s*') {
    throw "Release packaging must reject a staged QML tree that disagrees with the native manifest."
}
if (!(Test-Path (Join-Path $root "native/deploy-runtime.bat"))) {
    throw "MISSING: native/deploy-runtime.bat (the standalone-runtime deploy)."
}

Write-Host "Native runtime deployment checks passed."
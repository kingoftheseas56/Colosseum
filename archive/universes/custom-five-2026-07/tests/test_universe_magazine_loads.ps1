# MagazineUniversePage is behind the universe layer's lazy Loader — this test actually
# instantiates the page headless and requires LOADER READY (the lazy-page load gate).
# It also runs the registry pure-logic harness (exit-code verdict).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$env:QT_FORCE_STDERR_LOGGING = "1"

# 1) pure logic: mapEntry / bucketByEra / fmtMembers
$logic = Join-Path $PSScriptRoot "magazine_registry_harness.qml"
cmd /c "`"$qmlExe`" -platform offscreen `"$logic`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "magazine registry logic harness failed (exit $LASTEXITCODE)" }

# 2) creation: the page instantiates behind a Loader
$harness = Join-Path $PSScriptRoot "universe_magazine_load_harness.qml"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String
if ($out -notlike "*LOADER READY*") {
    throw "MagazineUniversePage failed to instantiate. Loader output:`n$out"
}

Write-Host "universe magazine page load + registry logic: OK"

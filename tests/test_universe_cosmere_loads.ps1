$ErrorActionPreference = "Stop"

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $PSScriptRoot "universe_cosmere_load_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $out -notlike "*LOADER READY*") {
    throw "CosmereUniversePage failed to instantiate. Loader output:`n$out"
}
Write-Host "universe Cosmere page load: OK"


$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (-not (Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe" }
$harness = Join-Path $root "tests/comic_gc_sources_logic_harness.qml"
# cmd /c + QT_FORCE_STDERR_LOGGING: PowerShell's native `&`/2>&1 redirection does not
# reliably capture qml.exe's (GUI-subsystem) stdout on this machine -- same workaround
# already proven in tests/test_comics_catalog_v1.ps1.
$env:QT_FORCE_STDERR_LOGGING = "1"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "GC sources logic harness failed (exit $LASTEXITCODE):`n$output" }
if ($output -notlike "*GC-SOURCES-LOGIC OK*") { throw "harness ran but verdict line missing:`n$output" }
Write-Host "comic gc sources page contract OK"

# Deterministic gate for MangaSeries.qml's catalogue-fed identity + masthead
# (catalogue-independence Slice 2, amended 2026-08-20). Mirrors test_manga_reading_room.ps1.
param([switch]$Rebuilt)

$ErrorActionPreference = "Stop"
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$previousForceStderr = $env:QT_FORCE_STDERR_LOGGING
$previousQmlRules = $env:QT_LOGGING_RULES
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_LOGGING_RULES = "qt.qml.*=false"
$qmlArguments = @("-platform", "offscreen", (Join-Path $PSScriptRoot "manga_series_catalogue_harness.qml"))
if ($Rebuilt) { $qmlArguments += @("--", "--rebuilt") }
$output = & $qmlExe @qmlArguments 2>&1 | Out-String
$code = $LASTEXITCODE
$env:QT_FORCE_STDERR_LOGGING = $previousForceStderr
$env:QT_LOGGING_RULES = $previousQmlRules
$ErrorActionPreference = $previous

if ($code -ne 0 -or $output -notmatch "MANGA_SERIES_CATALOGUE_OK") {
    Write-Host "FAIL: manga series catalogue harness (exit $code)"
    Write-Host $output
    exit 1
}

if ($Rebuilt) { Write-Host "manga series catalogue (rebuilt): OK" }
else { Write-Host "manga series catalogue: OK" }

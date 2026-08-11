# Deterministic gate for the locked Tankoban Reading Room redesign.
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
$output = & $qmlExe -platform offscreen (Join-Path $PSScriptRoot "manga_reading_room_harness.qml") 2>&1 | Out-String
$code = $LASTEXITCODE
$env:QT_FORCE_STDERR_LOGGING = $previousForceStderr
$env:QT_LOGGING_RULES = $previousQmlRules
$ErrorActionPreference = $previous

if ($code -ne 0 -or $output -notmatch "MANGA_READING_ROOM_OK") {
    Write-Host "FAIL: Reading Room harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "manga Reading Room: OK"

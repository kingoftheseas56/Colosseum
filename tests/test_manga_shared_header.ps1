$ErrorActionPreference = "Stop"
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { Write-Host "FAIL: qml.exe missing"; exit 1 }
$oldForce = $env:QT_FORCE_STDERR_LOGGING
$oldRules = $env:QT_LOGGING_RULES
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_LOGGING_RULES = "qt.qml.*=false"
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen (Join-Path $PSScriptRoot "manga_shared_header_harness.qml") 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = "Stop"
$env:QT_FORCE_STDERR_LOGGING = $oldForce
$env:QT_LOGGING_RULES = $oldRules
if ($code -ne 0 -or $output -notmatch "MANGA_SHARED_HEADER_OK") {
    Write-Host "FAIL: manga shared header (exit $code)"
    Write-Host $output
    exit 1
}
Write-Host "manga shared header: OK"
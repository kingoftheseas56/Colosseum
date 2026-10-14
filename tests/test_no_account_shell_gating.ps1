# Deterministic gate for the 1.1.9 no-account edition's QML entry-surface
# gating. Loads the REAL TopBar.qml and AccountOnboardingHost.qml with fake
# controllers; mirrors test_manga_series_catalogue.ps1.
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
$output = & $qmlExe -platform offscreen (Join-Path $PSScriptRoot "no_account_shell_gating_harness.qml") 2>&1 | Out-String
$code = $LASTEXITCODE
$env:QT_FORCE_STDERR_LOGGING = $previousForceStderr
$env:QT_LOGGING_RULES = $previousQmlRules
$ErrorActionPreference = $previous

if ($code -ne 0 -or $output -notmatch "NO_ACCOUNT_SHELL_GATING_OK") {
    Write-Host "FAIL: no-account shell gating harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "no-account shell gating: OK"
exit 0

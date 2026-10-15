$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_QUICK_CONTROLS_STYLE = "Basic"
$env:QT_QPA_FONTDIR = "C:\Windows\Fonts"
$harness = Join-Path $root "tests\account_recovery_key_ack_harness.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
$code = $LASTEXITCODE
Write-Host $output
if ($code -ne 0 -or $output -notlike "*ACCOUNT_RECOVERY_KEY_ACK_OK*") {
    throw "account recovery-key acknowledgement harness failed (exit $code):`n$output"
}
Write-Host "account recovery-key acknowledgement OK"

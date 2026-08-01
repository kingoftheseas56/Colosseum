$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Runs the pure-policy QML harness offscreen and gates on its OK marker + a clean exit.
# qml.exe is a GUI-subsystem exe: its console.log never reaches redirected stdout unless
# QT_FORCE_STDERR_LOGGING forces it to stderr, so we set that and capture the merged stream.
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $root "tests\explicit_content_policy_harness.qml"
$qmlInc  = Join-Path $root "qml"
$output  = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$harness`" 2>&1" | Out-String
$code    = $LASTEXITCODE

Write-Host $output

if ($code -ne 0 -or $output -notlike "*EXPLICIT_CONTENT_POLICY_OK*") {
    throw "Explicit content policy harness failed (exit $code):`n$output"
}

Write-Host "explicit content policy: OK"
exit 0

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $PSScriptRoot "hotkey_registry_harness.qml"
# The harness sets its verdict via exit code (Qt.exit(0) pass / non-zero fail); console output
# is not guaranteed to flush before exit, so the exit code is the source of truth.
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "hotkey registry harness failed (exit $LASTEXITCODE):`n$out" }
Write-Host "Player hotkey registry logic checks passed."

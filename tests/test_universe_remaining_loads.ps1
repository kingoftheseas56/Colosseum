$ErrorActionPreference = 'Stop'
$qmlExe = 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe'
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe" }
$harness = Join-Path $PSScriptRoot 'universe_remaining_load_harness.qml'
$env:QT_FORCE_STDERR_LOGGING = '1'
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String
if ($out -notlike '*ALL K04 REMAINING LOADERS READY*') {
    throw "one or more remaining K-04 pages failed to instantiate:`n$out"
}
Write-Host 'universe generic + extension + studio + LOCG + tile loads: OK'

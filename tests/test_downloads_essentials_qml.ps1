$ErrorActionPreference = 'Stop'

$qmlExe = 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe'
if (!(Test-Path -LiteralPath $qmlExe)) {
    throw "qml.exe not found at $qmlExe"
}

$harness = Join-Path $PSScriptRoot 'downloads_essentials_harness.qml'
$env:QT_FORCE_STDERR_LOGGING = '1'
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $out -notlike '*DOWNLOADS ESSENTIALS PASS*') {
    throw "Downloads essentials QML harness failed (exit $LASTEXITCODE):`n$out"
}

Write-Host 'downloads essentials QML harness: OK'

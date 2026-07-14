$ErrorActionPreference = "Stop"

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "world_search_comics_catalog_probe.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $output -notlike "*WORLD_SEARCH_COMICS_OK 688*") {
    throw "WorldSearch comics catalog probe failed (exit $LASTEXITCODE):`n$output"
}

Write-Host "world search comics catalog: OK"

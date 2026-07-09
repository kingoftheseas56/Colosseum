$ErrorActionPreference = "Stop"
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $PSScriptRoot "scroll_glide_harness.qml"
# Verdict rides the exit code (Qt.exit(0) pass / non-zero fail).
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "scroll glide load-gate failed (exit $LASTEXITCODE):`n$out" }
Write-Host "test_scroll_glide_p0 PASS"

$root = Split-Path -Parent $PSScriptRoot
foreach ($f in @("WorldPage","DownloadsPage","ComicSeries","SearchSurface")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*") { throw "$f missing ScrollGlide" }
    if ($c -notlike "*HouseScrollBar*") { throw "$f missing HouseScrollBar" }
}
Write-Host "scroll sweep contract PASS"

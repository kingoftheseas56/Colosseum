$ErrorActionPreference = "Stop"
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $PSScriptRoot "scroll_glide_harness.qml"
# Verdict rides the exit code (Qt.exit(0) pass / non-zero fail).
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "scroll glide load-gate failed (exit $LASTEXITCODE):`n$out" }
Write-Host "test_scroll_glide_p0 PASS"

$root = Split-Path -Parent $PSScriptRoot
$bar = Get-Content (Join-Path $root "qml/HouseScrollBar.qml") -Raw
# Hemanth 2026-07-12: the hover-revealed thumb read as an ugly white bar on every page — REMOVED
# app-wide. HouseScrollBar is now an inert, no-draw ScrollBar (pages scroll by wheel/drag). This
# contract flipped from "only a subtle hover thumb" to "no visible bar at all" by his direct call.
if ($bar -notlike "*ScrollBar.AlwaysOff*") { throw "HouseScrollBar must be AlwaysOff - no visible bar (removed 2026-07-12)." }
if ($bar -like "*id: thumb*") { throw "HouseScrollBar must not draw a thumb anymore (removed 2026-07-12)." }
if ($bar -like "*Qt.rgba(1, 1, 1, 0.46)*") { throw "HouseScrollBar must not paint the old white hover thumb." }
if ($bar -like "*Qt.rgba(1, 1, 1, 0.34)*") { throw "HouseScrollBar must not paint the old white always-visible rail." }

foreach ($f in @("WorldPage","DownloadsPage","ComicSeries","SearchSurface")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*") { throw "$f missing ScrollGlide" }
    if ($c -notlike "*HouseScrollBar*") { throw "$f missing HouseScrollBar" }
}
Write-Host "scroll sweep contract PASS"

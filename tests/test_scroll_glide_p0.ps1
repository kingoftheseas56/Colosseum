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
# Hemanth 2026-07-12: white slab REMOVED, then he asked for a subtle GOLD sliver that shows only
# while actively scrolling. Contract now: a thin gold thumb, motion-revealed (flick.moving), no white.
if ($bar -notlike "*id: thumb*") { throw "HouseScrollBar must draw its gold sliver thumb." }
if ($bar -notlike "*color: theme.gold*") { throw "HouseScrollBar thumb must be gold, not white." }
if ($bar -notlike "*flick.moving*") { throw "HouseScrollBar must reveal from scroll motion (flick.moving/flicking), not always-on." }
if ($bar -notlike "*bar.visualPosition*") { throw "HouseScrollBar thumb must follow ScrollBar visualPosition." }
if ($bar -notlike "*bar.visualSize*") { throw "HouseScrollBar thumb must size from ScrollBar visualSize." }
if ($bar -like "*Qt.rgba(1, 1, 1, 0.46)*") { throw "HouseScrollBar must not paint the old white hover thumb." }
if ($bar -like "*Qt.rgba(1, 1, 1, 0.34)*") { throw "HouseScrollBar must not paint the old white always-visible rail." }

foreach ($f in @("WorldPage","DownloadsPage","ComicSeries","SearchSurface")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*") { throw "$f missing ScrollGlide" }
    if ($c -notlike "*HouseScrollBar*") { throw "$f missing HouseScrollBar" }
}
Write-Host "scroll sweep contract PASS"

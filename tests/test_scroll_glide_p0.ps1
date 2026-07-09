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
if ($bar -notlike "*HoverHandler*") { throw "HouseScrollBar must reveal from edge hover, not from normal scroll." }
if ($bar -like "*bar.active*") { throw "HouseScrollBar must not reveal from ScrollBar.active during normal wheel scroll." }
if ($bar -notlike "*anchors.rightMargin: 0*") { throw "HouseScrollBar must attach flush to the right edge." }
if ($bar -notlike "*id: thumb*") { throw "HouseScrollBar must draw only its own hover-revealed thumb." }
if ($bar -notlike "*bar.visualPosition*") { throw "HouseScrollBar thumb must follow ScrollBar visualPosition." }
if ($bar -notlike "*bar.visualSize*") { throw "HouseScrollBar thumb must size from ScrollBar visualSize." }
if ($bar -notlike "*background: Rectangle*") { throw "HouseScrollBar must explicitly override the styled background." }
if ($bar -notlike '*color: "transparent"*') { throw "HouseScrollBar background must be transparent." }
if ($bar -notlike "*visible: false*") { throw "HouseScrollBar styled background/content must stay invisible." }
if ($bar -like "*Qt.rgba(1, 1, 1, 0.34)*") { throw "HouseScrollBar must not leave a white always-visible rail/holding bar." }

foreach ($f in @("WorldPage","DownloadsPage","ComicSeries","SearchSurface")) {
    $c = Get-Content (Join-Path $root "qml/$f.qml") -Raw
    if ($c -notlike "*ScrollGlide*") { throw "$f missing ScrollGlide" }
    if ($c -notlike "*HouseScrollBar*") { throw "$f missing HouseScrollBar" }
}
Write-Host "scroll sweep contract PASS"

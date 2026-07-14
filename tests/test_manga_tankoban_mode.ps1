# Contract + offscreen-logic gate for Tankoban "volume mode" (Task 9).
#
# Two layers, both cheap and CI-safe:
#   1. GREP SHAPE — the three QML files must carry the load-bearing wiring strings
#      (a green grep proves the string is PRESENT, never that it behaves; the
#      offscreen harness is what proves behaviour).
#   2. OFFSCREEN LOGIC — qml.exe -platform offscreen drives MangaTankobanLibrary
#      against a FAKE TankobanVolumes for TWO series and must print the sentinel.
#
# The final pixels are Hemanth's eyes-on; this file only pins shape + logic.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        Write-Host "FAIL: $message"
        exit 1
    }
}

$series  = Read-RepoFile "qml/MangaSeries.qml"
$library = Read-RepoFile "qml/MangaTankobanLibrary.qml"
$card    = Read-RepoFile "qml/MangaTankobanSourceCard.qml"

Assert-Contains $series 'text: "TANKOBAN MODE"' "series-level mode label missing"
Assert-Contains $series 'TankobanVolumes.prepareSeries' "dynamic snapshot is not handed off"
Assert-Contains $series 'MangaTankobanLibrary {' "volume-first surface missing"
Assert-Contains $library 'model: root.volumeRows' "all canonical volumes must render"
Assert-Contains $library 'TankobanVolumes.searchSources' "volume click must open sources"
Assert-Contains $card 'modelData.uploader' "uploader evidence must remain visible"
Assert-Contains $card 'modelData.seeders' "seed evidence must remain visible"
Assert-Contains $card 'Build from chapters' "WeebCentral fallback copy missing"

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "manga_tankoban_page_harness.qml"
# qml.exe emits benign warnings (font dir) on stderr; don't let ErrorActionPreference=Stop
# turn a native-command stderr line into a terminating error before we read the verdict.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP
if ($code -ne 0 -or ($output -notmatch "MANGA_TANKOBAN_PAGE_OK")) {
    Write-Host "FAIL: offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "manga tankoban mode: OK"

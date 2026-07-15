# Contract + offscreen-logic gate for Tankoban "volume mode" (Tasks 9–10).
#
# Two layers, both cheap and CI-safe:
#   1. GREP SHAPE — the QML files must carry the load-bearing wiring strings
#      (a green grep proves the string is PRESENT, never that it behaves; the
#      offscreen harness is what proves behaviour).
#   2. OFFSCREEN LOGIC — qml.exe -platform offscreen drives MangaTankobanLibrary
#      against a FAKE TankobanVolumes for TWO series AND drives the generalized
#      MangaReader over an injected page store, and must print the sentinel.
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
$page    = Read-RepoFile "qml/MangaTankobanSourcesPage.qml"
$reader  = Read-RepoFile "qml/MangaReader.qml"
$main    = Read-RepoFile "qml/Main.qml"

Assert-Contains $series 'text: "TANKOBAN MODE"' "series-level mode label missing"
Assert-Contains $series 'TankobanVolumes.prepareSeries' "dynamic snapshot is not handed off"
Assert-Contains $series 'MangaTankobanLibrary {' "volume-first surface missing"
Assert-Contains $series 'MangaTankobanSourcesPage {' "full-screen sources page must be hosted"
Assert-Contains $library 'model: root.volumeRows' "all canonical volumes must render"
Assert-Contains $library 'signal sourcesRequested' "library must emit a full-screen sources request"
# --- the full-screen sources picker (replaces the inline MangaTankobanSourceCard) ---
Assert-Contains $page 'text: "SOURCES' "sources page gold eyebrow missing"
Assert-Contains $page 'searchSources' "sources page must kick a source search"
Assert-Contains $page 'modelData.uploader' "uploader evidence must remain visible"
Assert-Contains $page 'modelData.seeders' "seed evidence must remain visible"
Assert-Contains $page 'Build from chapters' "WeebCentral fallback copy missing"
Assert-Contains $page 'downloadNyaa' "a Nyaa pick must call downloadNyaa"
Assert-Contains $page 'compileWeebCentral' "a WeebCentral pick must call compileWeebCentral"

# --- Task 10: the generalized reader contract (chapter reading must NOT regress) ---
Assert-Contains $reader 'property var pageStore: null' "reader must accept an injected page store"
Assert-Contains $reader 'property string entryKind: "manga"' "reader must default entryKind to manga"
Assert-Contains $reader 'readonly property var store: pageStore ? pageStore' "injected store must win, else the existing default store"
Assert-Contains $reader 'readonly property string progressKind: entryKind' "progress must namespace on entryKind"
Assert-Contains $reader 'signal sourceRequested(string entryId)' "reader must ask the series page for a not-ready volume's source"
# --- Task 10: the series page wires the volume open + the source-request escape ---
Assert-Contains $series 'onOpenVolumeRequested' "library Open action must reach the reader"
Assert-Contains $series 'onSourceRequested' "reader source request must reach the series page"
Assert-Contains $series 'entryKind: page.openEntryKind' "the reader is fed the entry kind"
# --- Task 10: a saved tankoban record resumes with Tankoban Mode ON ---
Assert-Contains $main 'entry.kind === "tankoban"' "Main must route a saved tankoban record"
Assert-Contains $main 'resumeTankobanVolume' "Main must resume the manga series into the volume"

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

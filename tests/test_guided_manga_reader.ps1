# Contract + offscreen-logic gate for Guided as the FIFTH MangaReader style (Task 10).
#
# Two layers, both cheap and CI-safe (no build):
#   1. GREP SHAPE - MangaReader must carry the load-bearing Guided wiring strings (a green
#      grep proves the string is PRESENT, never that it behaves; the harness proves behaviour).
#   2. OFFSCREEN LOGIC - qml.exe -platform offscreen drives the REAL MangaReader against a fake
#      page store + fake GuidedAnalysis, with the native camera controller supplied by a QML
#      mock on the -I import path (tests/qmlmock). The final pixels are Hemanth's eyes-on;
#      this pins shape + integration logic only.

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
function Assert-Absent([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) {
        Write-Host "FAIL: $message"
        exit 1
    }
}

$reader   = Read-RepoFile "qml/MangaReader.qml"
$viewport = Read-RepoFile "qml/guided/GuidedViewport.qml"
$controls = Read-RepoFile "qml/guided/GuidedControls.qml"
$hostwrap = Read-RepoFile "qml/guided/GuidedCameraHost.qml"

# --- the fifth style is offered + labelled ---
Assert-Contains $reader '{v:"guided",t:"Guided"}' "Guided must be the fifth entry in the mode selector"
Assert-Contains $reader 'if (s === "guided") return "Guided"' "modeShort must label the Guided style"
Assert-Contains $reader 'readonly property bool guided: style === "guided"' "a dedicated guided predicate, NOT folded into paged"
# --- guided is a separate surface, not a paged/strip fall-through ---
Assert-Contains $reader 'function enterGuided()' "enterGuided must build the canvas model"
Assert-Contains $reader 'function exitGuided()' "exitGuided must restore the prior style"
Assert-Contains $reader 'function buildGuidedCanvases()' "the guided canvas model builder"
Assert-Contains $reader 'function guidedAdvanceCanvas()' "canvas-level forward crossing"
Assert-Contains $reader 'function interruptGuided(' "reading gestures must interrupt Auto Read"
# --- mounted surfaces + the defensive controller loader ---
Assert-Contains $reader 'import "guided"' "MangaReader must import the guided QML surfaces"
Assert-Contains $reader 'GuidedViewport {' "the intact-canvas viewport must be mounted"
Assert-Contains $reader 'GuidedControls {' "the guided transport must be mounted"
Assert-Contains $reader 'id: guidedCameraLoader' "the controller must load through a Loader"
Assert-Contains $reader 'source: "guided/GuidedCameraHost.qml"' "the Loader hosts the native controller wrapper"
# MangaReader must NOT hard-import the native module (would break the reader before A0 wires main.cpp)
Assert-Absent $reader 'import Colosseum.Guided' "MangaReader must NOT import Colosseum.Guided directly; load it via the Loader"
# --- viewport keeps sources intact; host isolates the native import ---
Assert-Contains $viewport 'cropItemCount: 0' "the viewport must never fabricate cropped-panel images"
Assert-Contains $controls 'signal exitRequested()' "the transport must offer Exit Guided"
Assert-Contains $hostwrap 'import Colosseum.Guided' "the host wrapper is the one place that imports the native module"

# --- offscreen behaviour, with the mock controller on the import path ---
$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "guided_manga_reader_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "GUIDED_MANGA_READER_OK")) {
    Write-Host "FAIL: guided manga reader offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "guided manga reader: OK"

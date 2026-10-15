# Contract gate for the maximum-height continuum mock now used by production.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) { Get-Content -Raw -LiteralPath (Join-Path $root $relativePath) }
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}
function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}

$main = Read-RepoFile "qml/Main.qml"
$room = Read-RepoFile "qml/MangaReadingRoom.qml"
$library = Read-RepoFile "qml/MangaTankobanLibrary.qml"

Assert-Contains $main '|| seriesLayer.active' "the manga series surface must suppress the taskbar"
Assert-Contains $room 'id: storyMasthead' "the series context must use the compact story masthead"
Assert-Contains $room 'id: storyActions' "the masthead must own progress and open actions"
Assert-NotContains $room 'id: storySynopsis' "the approved Pages-flow masthead has no synopsis panel"
Assert-Contains $room '../assets/icons/play-dark.svg' "open-volume action must use the black play SVG"
Assert-Contains $library 'id: continuum' "volume mode must use the continuum"
Assert-Contains $library 'id: volumeFlow' "volume mode must use the reader-derived Pages flow"
Assert-Contains $library 'id: continuumRange' "the continuum must have a bottom scrubber"
Assert-Contains $library 'Download next 10' "the batch action must use the mock copy"
Assert-Contains $library 'Download selected' "selection mode needs an explicit download action"
Assert-Contains $library 'function sizeContinuumBooks()' "book height must derive from the continuum"
Assert-Contains $library 'model: root.activeTab === "volumes" ? root.volumeRows : []' "the volume flow must retain the canonical model only while visible"
Assert-NotContains $library 'Get next 10 missing' "the confusing bulk-download copy must be gone"
Assert-NotContains $library 'id: jumpStrip' "the old vertical jump rail must be gone"
Assert-NotContains $library 'id: detailMenu' "the unrequested Details menu must be gone"
Assert-NotContains $library 'text: \"Details\"' "volume tiles must not add a Details button"

Write-Host "maximum-height Reading Room contract: OK"

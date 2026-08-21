# Regression contract for the maximum-height continuum implementation.
$ErrorActionPreference = "Stop"

function Read-RepoFile([string]$relativePath) { Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot ("..\" + $relativePath)) }
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}
function Assert-Absent([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}

$library = Read-RepoFile "qml\MangaTankobanLibrary.qml"
$room = Read-RepoFile "qml\MangaReadingRoom.qml"
$series = Read-RepoFile "qml\MangaSeries.qml"

Assert-Contains $library 'id: continuum' "volume mode must expose the continuum surface"
Assert-Contains $library 'id: volumeFlow' "the complete canonical model must feed the ListView flow"
Assert-Contains $library 'function scaleForIndex(index)' "the volume ramp must be monotonic by distance from focus"
Assert-Contains $library 'function centreNow()' "the flow must retain the reader's centering operation"
Assert-Absent $library 'id: continuumRepeater' "the hand-positioned Repeater must not return"
Assert-Contains $library 'function sizeContinuumBooks()' "focused book sizing must use available height"
Assert-Contains $library 'id: continuumRange' "the bottom focus scrubber must be present"
Assert-Contains $library 'Accessible.onIncreaseAction' "the scrubber must be operable through slider actions"
Assert-Contains $room 'id: storyMasthead' "the old split rail must be replaced by the story masthead"
Assert-Contains $series 'if (!page.synopsis.length && d.description && d.description.length)' "detail description must backfill a missing synopsis"
Assert-Absent $library 'id: jumpStrip' "the vertical index rail must not return"
Assert-Absent $library 'id: detailMenu' "the Details menu must not return"

Write-Host "maximum-height continuum volume wall: OK"

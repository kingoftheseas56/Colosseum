# Contract gate for the supplied maximum-height continuum mock.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}
function Assert-Absent([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 }
}

$room = Read-RepoFile "qml/MangaReadingRoom.qml"
$library = Read-RepoFile "qml/MangaTankobanLibrary.qml"

Assert-Contains $room 'id: storyMasthead' "Reading Room must use the compressed story masthead"
Assert-Contains $room 'id: storyActions' "the story masthead must own the progress actions"
Assert-Contains $room 'id: storyTitle' "the series title must have its own masthead row"
Assert-Contains $room 'anchors.topMargin: storyTitle.implicitHeight + 12' "metadata must begin below the series title"
Assert-Contains $room 'id: deviceCount' "device progress must have its own masthead row"
Assert-Contains $room 'id: deviceProgress' "device progress must have its own masthead row"
Assert-Contains $room 'anchors.top: deviceProgress.bottom' "masthead actions must begin below device progress"
Assert-Absent $room 'text: "MANGA \\u00b7 TANKOBAN"' "the redundant masthead eyebrow must be gone"
Assert-Absent $room 'id: storySynopsis' "the Pages-flow Reading Room must not show a synopsis panel"
Assert-Absent $room 'id: rail' "the old fixed cover rail must be gone"

Assert-Contains $library 'id: continuum' "volume mode must render the continuum surface"
Assert-Contains $library 'id: volumeFlow' "the continuum must use the reader-style ListView flow"
Assert-Contains $library 'function centreNow()' "the flow must center only through its owned operation"
Assert-Contains $library 'positionViewAtIndex(i, ListView.Center)' "the focused volume must center by ListView geometry"
Assert-Contains $library 'model: root.activeTab === "volumes" ? root.volumeRows : []' "inactive volume mode must gate the ListView model"
Assert-Absent $library 'id: continuumRepeater' "the manual Repeater continuum must be gone"
Assert-Contains $library 'function sizeContinuumBooks()' "focused book sizing must derive from available height"
Assert-Contains $library 'property var focusNumber' "the continuum must expose a focused volume"
Assert-Contains $library 'property string focusToken' "the continuum must preserve canonical volume identity"
Assert-Contains $library 'id: tileAction' "volume presses must have a real focus target"
Assert-Contains $library 'id: continuumRange' "the continuum must expose a bottom range control"
Assert-Contains $library 'Accessible.role: Accessible.Slider' "the focus scrubber must be keyboard and accessibility aware"
Assert-Contains $library 'Accessible.onIncreaseAction' "the focus scrubber must expose the slider increase action"
Assert-Contains $library 'Accessible.onDecreaseAction' "the focus scrubber must expose the slider decrease action"
Assert-Contains $library 'Utility controls belong in one quiet line' "the secondary row must stay utility-only"
Assert-Absent $library 'VOLUME FLOW' "the duplicate volume-flow heading must be gone"
Assert-Absent $library 'LOOSE CHAPTERS' "the duplicate chapter heading must be gone"
Assert-Absent $library 'id: jumpStrip' "the old vertical index rail must be gone"
Assert-Absent $library 'id: detailMenu' "the unrequested Details menu must be gone"
Assert-Absent $library 'text: \"Details\"' "volume tiles must not add a Details button"

Write-Host "maximum-height continuum contract: OK"

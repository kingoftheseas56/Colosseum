$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$sub = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Bug: the footer ("Find more subtitles" / "Load file") and the "Back to tracks"
# button anchored their bottom to delayRow.top, but delayRow is not their sibling
# (it's a child of the pane; they are grandchildren via an inner Item). QML drops
# the invalid anchor, collapsing the footer onto the top filter tabs = text bleed.

Assert-NotContains $sub "anchors.bottom: delayRow.top" `
    "Footer must not anchor to delayRow (not a sibling) - that invalid anchor caused the overlap."
Assert-Contains $sub "anchors.bottomMargin: delayRow.height" `
    "Footer must sit above the delay row using its height as a bottom margin."

Write-Host "Subtitle menu footer layout contract checks passed."

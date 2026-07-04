$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: the player quick tools need A-B repeat with hotkeys and a visible chip.
Assert-Contains $player "property real abLoopA" `
    "PlayerPage must track the A point for A-B repeat."
Assert-Contains $player "property real abLoopB" `
    "PlayerPage must track the B point for A-B repeat."
Assert-Contains $player "readonly property bool abLoopActive" `
    "PlayerPage must expose whether the A-B loop is active."
Assert-Contains $player "function setAbLoopA" `
    "PlayerPage must expose an action to set the A point."
Assert-Contains $player "function setAbLoopB" `
    "PlayerPage must expose an action to set the B point."
Assert-Contains $player "function clearAbLoop" `
    "PlayerPage must expose an action to clear the loop."
Assert-Contains $player "Qt.Key_I" `
    "PlayerPage must match Harbor's I hotkey for setting loop A."
Assert-Contains $player "Qt.Key_O" `
    "PlayerPage must match Harbor's O hotkey for setting loop B."
Assert-Contains $player "Qt.Key_L" `
    "PlayerPage must match Harbor's L hotkey for clearing A-B repeat."
Assert-Contains $player "mpv.position >= root.abLoopB" `
    "PlayerPage must seek back to A when playback crosses B."
Assert-Contains $player "root.seekTo(root.abLoopA)" `
    "PlayerPage must use the normal seek path when looping back to A."
Assert-Contains $player "A-B loop" `
    "PlayerPage must render a visible A-B loop chip."
Assert-Contains $player "tooltip: `"Clear A-B loop`"" `
    "PlayerPage must expose a clear action on the A-B loop chip."

Write-Host "Player A-B loop P0 parity contract checks passed."

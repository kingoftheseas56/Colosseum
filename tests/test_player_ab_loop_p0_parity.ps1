$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$hotkeys = Get-Content (Join-Path $root "qml/PlayerHotkeys.js") -Raw

function Assert-Contains($text, $needle, $message) {
    # Literal substring match: -like would treat bracket needles (e.g. ["I"]) as wildcard classes.
    if (-not $text.Contains($needle)) {
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
# A-B loop hotkeys are registry-backed now (Feature 7): the I/O/L bindings live in
# PlayerHotkeys.js and dispatch through runHotkeyAction, not a raw Keys.onPressed cascade.
Assert-Contains $hotkeys 'id: "abLoopA"' `
    "PlayerHotkeys must define the loop-A action."
Assert-Contains $hotkeys 'id: "abLoopB"' `
    "PlayerHotkeys must define the loop-B action."
Assert-Contains $hotkeys 'id: "abLoopClear"' `
    "PlayerHotkeys must define the clear-loop action."
Assert-Contains $hotkeys 'bindings: ["I"]' `
    "Loop A must keep Harbor's I hotkey."
Assert-Contains $hotkeys 'bindings: ["O"]' `
    "Loop B must keep Harbor's O hotkey."
Assert-Contains $hotkeys 'bindings: ["L"]' `
    "Clear loop must keep Harbor's L hotkey."
Assert-Contains $player 'case "abLoopA": root.setAbLoopA()' `
    "PlayerPage must dispatch loop A through runHotkeyAction."
Assert-Contains $player 'case "abLoopB": root.setAbLoopB()' `
    "PlayerPage must dispatch loop B through runHotkeyAction."
Assert-Contains $player 'case "abLoopClear": root.clearAbLoop()' `
    "PlayerPage must dispatch clear loop through runHotkeyAction."
Assert-Contains $player "mpv.position >= root.abLoopB" `
    "PlayerPage must seek back to A when playback crosses B."
Assert-Contains $player "root.seekTo(root.abLoopA)" `
    "PlayerPage must use the normal seek path when looping back to A."
Assert-Contains $player "A-B loop" `
    "PlayerPage must render a visible A-B loop chip."
Assert-Contains $player "tooltip: `"Clear A-B loop`"" `
    "PlayerPage must expose a clear action on the A-B loop chip."

Write-Host "Player A-B loop P0 parity contract checks passed."

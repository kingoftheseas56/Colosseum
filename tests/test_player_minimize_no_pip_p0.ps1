$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Fullscreen-only shell: a fullscreen toggle and a floating PiP window both fight
# the always-fullscreen model. Replace the fullscreen button with a Minimize
# button (pauses + minimizes the app); remove the PiP button entirely.

# --- ONE honest minimize: the top-left session control, nothing else ---
Assert-Contains $player 'icon: "minimizeToBar"' `
    "The top-left session minimize (minimizeToBar) must exist."
Assert-Contains $player 'kind === "minimizeToBar"' `
    "IconGlyph must draw the 'minimizeToBar' glyph."
Assert-Contains $player 'your spot is saved in the taskbar' `
    "Minimize tooltip must be honest: state is captured, not kept playing."
$minCalls = [regex]::Matches($player, 'root\.minimizeRequested\(\)').Count
if ($minCalls -ne 1) { throw "Exactly ONE control must call minimizeRequested() (found $minCalls)." }
Assert-NotContains $player 'kind === "minimize") {' `
    "The dock 'minimize' glyph branch must be gone (dock minimize removed)."
Assert-NotContains $player 'keeps playing in the taskbar' `
    "The old overpromising tooltip must be gone."

# --- Fullscreen button gone ---
Assert-NotContains $player 'icon: "fullscreen"' `
    "The Fullscreen button must be removed (the app is always fullscreen)."

# --- PiP removed: no button, no exit overlay, no toggle, no hotkey ---
Assert-NotContains $player 'icon: "pip"' `
    "The Picture-in-Picture button and exit overlay must be removed."
Assert-NotContains $player "function togglePipMode" `
    "The PiP toggle function must be removed."
Assert-NotContains $player "Qt.Key_P)" `
    "The PiP keyboard shortcut must be removed."

Write-Host "Player minimize / no-PiP contract checks passed."

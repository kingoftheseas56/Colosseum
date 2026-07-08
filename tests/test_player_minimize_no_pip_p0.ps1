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
# the always-fullscreen model. The ONE minimize control sits beside Close in the control
# bar (pauses but keeps the stream warm, drops to the taskbar); remove the PiP button entirely.

# --- ONE honest minimize: beside Close, nothing else ---
Assert-Contains $player 'icon: "minimizeToBar"' `
    "The session minimize (minimizeToBar) must exist."
Assert-Contains $player 'kind === "minimizeToBar"' `
    "IconGlyph must draw the 'minimizeToBar' glyph."
Assert-Contains $player 'Minimize — paused in the taskbar, resumes with no reload' `
    "Minimize tooltip must be honest: paused but kept warm, no reload on return."
$minCalls = [regex]::Matches($player, 'root\.minimizeRequested\(\)').Count
if ($minCalls -ne 1) { throw "Exactly ONE control must call minimizeRequested() (found $minCalls)." }
Assert-NotContains $player 'kind === "minimize") {' `
    "The dock 'minimize' glyph branch must be gone (dock minimize removed)."
Assert-NotContains $player 'keeps playing in the taskbar' `
    "The old overpromising tooltip must be gone."
Assert-NotContains $player 'your spot is saved in the taskbar' `
    "The old 'spot is saved' tooltip must be gone (minimize now keeps the stream warm)."
Assert-Contains $player 'function suspendForMinimize' `
    "Minimize must pause via suspendForMinimize (keeps the stream warm, no reload)."

# --- minimize sits immediately before Close in the control bar (Hemanth 2026-07-07) ---
$minIdx   = $player.IndexOf('icon: "minimizeToBar"')
$closeIdx = $player.IndexOf('tooltip: "Close"')
if ($minIdx -lt 0 -or $closeIdx -lt 0) { throw "Both minimize and Close buttons must exist." }
if ($minIdx -ge $closeIdx) { throw "Minimize must come before Close." }
if (($closeIdx - $minIdx) -gt 900) { throw "Minimize must sit adjacent to Close in the control bar." }
Assert-NotContains $player 'id: backButton' `
    "The old top-left corner minimize (backButton) must be gone; minimize now lives beside Close."

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

# --- Fullscreen toggle removed (Feature 7): the app is always fullscreen ---
Assert-NotContains $player "Qt.Key_F" `
    "The fullscreen-only player must not keep an F fullscreen/window shortcut."
Assert-NotContains $player "toggleWindowFullscreen" `
    "The fullscreen-only player must not keep a window fullscreen toggle."

Write-Host "Player minimize / no-PiP contract checks passed."

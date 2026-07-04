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

# --- Minimize button + glyph present ---
Assert-Contains $player 'icon: "minimize"' `
    "The control bar must have a Minimize button in place of Fullscreen."
Assert-Contains $player 'kind === "minimize"' `
    "IconGlyph must draw a 'minimize' glyph."
Assert-Contains $player "root.minimizeRequested()" `
    "The Minimize button must minimize the whole app."

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

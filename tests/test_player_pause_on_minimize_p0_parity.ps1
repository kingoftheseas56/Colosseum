$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity: pause playback when the window is minimized (pauseMinimized:true),
# and auto-resume on restore -- but only ever undoing a pause we caused, and never
# while casting / in PiP / in a synced room (those are meant to keep playing).

# --- State ---
Assert-Contains $player "property bool autoPausedInactive" `
    "PlayerPage must track a pause it caused, so resume never overrides a manual pause."
Assert-Contains $player "property bool windowMinimized" `
    "PlayerPage must derive whether its window is currently minimized."
Assert-Contains $player "root.Window.window.visibility === Window.Minimized" `
    "Minimized detection must read the real window visibility."

# --- Reactive handler ---
Assert-Contains $player "function handleWindowMinimize" `
    "PlayerPage must react to minimize/restore transitions."
Assert-Contains $player "onWindowMinimizedChanged" `
    "PlayerPage must invoke the handler when minimized state flips."

# --- Guards: don't fight PiP / casting / room-sync ---
Assert-Contains $player "if (root.pipMode)" `
    "Pause-on-minimize must not fire during PiP (PiP is meant to keep playing)."
Assert-Contains $player "Cast.active" `
    "Pause-on-minimize must not fire while casting to another device."

# --- Pause / resume behavior ---
Assert-Contains $player "root.autoPausedInactive = true" `
    "On minimize the player must pause and mark the pause as self-caused."
Assert-Contains $player "root.autoPausedInactive = false" `
    "On restore the player must clear the self-caused pause flag before resuming."

Write-Host "Player pause-on-minimize P0 parity contract checks passed."

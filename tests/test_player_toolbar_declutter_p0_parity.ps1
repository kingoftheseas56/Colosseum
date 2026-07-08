$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Toolbar declutter v2 (Hemanth 2026-07-08): ONE overflow (...) menu on the bar, ever.
# The old ToolsMenu popover doubled the "..." next to the width-fold overflow, and it
# carried the cast/room parity stubs that are never shipping. The four REAL tools
# (screenshot / GIF / stats / draw) now live as rows in the single overflow panel.

# --- the old popover and the retired stubs are GONE ---
Assert-NotContains $player "component ToolsMenu" `
    "The old ToolsMenu popover must be gone (it doubled the ... button)."
Assert-NotContains $player "roomPanelOpen" `
    "Watch-room is retired - no panel, no state."
Assert-NotContains $player "castPanelOpen" `
    "Cast/share-stream is retired - no panel, no state."

# --- exactly one 'more' button remains, always reachable ---
Assert-Contains $player 'kind === "more"' `
    "IconGlyph must still draw the 'more' (...) glyph for the overflow button."

# --- the four real tools survive as overflow rows and actually fire ---
Assert-Contains $player '"kind": "screenshot"' `
    "Screenshot must live in the overflow panel."
Assert-Contains $player '"kind": "gif"' `
    "GIF recorder must live in the overflow panel."
Assert-Contains $player '"kind": "stats"' `
    "Playback stats must live in the overflow panel."
Assert-Contains $player '"kind": "draw"' `
    "Draw mode must live in the overflow panel."
Assert-Contains $player 'else if (kind === "screenshot") root.captureFrameGrab()' `
    "The screenshot row must actually fire the frame grab."
Assert-Contains $player '"Playback stats"' `
    "Playback stats row keeps its label."
Assert-Contains $player '"Screenshot"' `
    "Screenshot row keeps its label."

Write-Host "Player toolbar declutter (v2) checks passed."

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Fresh open: playTorrent must consult the identity-keyed store so a position saved
# under one torrent resumes under any other torrent of the same episode.
Assert-Contains $player 'Progress.get("video", root.mediaId)' `
    "playTorrent must read the saved position for the episode identity."
Assert-Contains $player "root.pendingSeekSec = savedPos" `
    "playTorrent must seed pendingSeekSec from the store (F3 overlay consumes it)."

# Mid-play switch: position carries silently (no overlay mid-watch).
Assert-Contains $player 'reason === "switch" && mpv.position > 0' `
    "playStreamAt must capture the live position on a stream switch."
Assert-Contains $player "root.resumePromptConsumed = true   // silent carry" `
    "Switch carry must suppress the resume overlay (silent seek)."

Write-Host "Cross-stream resume contract checks passed."

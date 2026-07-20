# Local (downloaded) playback must not paint the streaming buffer strip: the
# cache-fill layer on the seek bar is a STREAMING affordance (mpv's forced 60s
# disk read-ahead paints phantom "buffering" on local files otherwise).
# Bug: Hemanth eyes-on 2026-07-20 — downloaded Silo S3E2 showed a "buffer thingie".
# Plan: docs/superpowers/plans/2026-07-20-colosseum-play-while-arriving.md

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) { if ($text -notlike "*$needle*") { throw $message } }

# The cache layer still exists and still derives from mpv.cacheTime...
Assert-Contains $player 'mpv.cacheTime / mpv.duration' "Seek bar must keep the cacheTime-derived buffer layer for streams."
# ...but its visibility is gated off for local (downloaded) sessions.
Assert-Contains $player 'visible: width > 2 && root.mediaLocalPath.length === 0' "Buffer strip must hide when playing a local file (mediaLocalPath set)."

Write-Host "PASS: local playback hides the buffer strip; streams keep it."

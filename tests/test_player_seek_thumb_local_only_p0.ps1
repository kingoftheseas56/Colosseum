$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$thumb = Get-Content (Join-Path $root "native/player/seekthumbnailer.cpp") -Raw

# Seek-bar hover thumbnails are LOCAL-FILE ONLY (2026-09-19). A thumbnail is
# `ffmpeg -ss N -i <src> -frames:v 1`; over a torrent bridge URL the seek becomes a
# byte-range the swarm may not have, so ffmpeg stalls the full 10s kill window and the
# spawn/kill cycle repeats on every hover — pure churn behind the film (session-log
# evidence 2026-09-18). Streams keep the timestamp-only tooltip.

if ($thumb -notlike "*if (!source.isLocalFile())*" -and
    $thumb -notlike "*if (!source.isLocalFile ())*") {
    throw "SeekThumbnailer::request must refuse non-local sources before any spawn."
}
if ($thumb -notlike "*startJob*" -or $thumb -notlike "*kStallMs*") {
    throw "SeekThumbnailer must keep its job transport and stall-kill timer intact."
}

Write-Host "Seek thumbnailer local-only P0 contract checks passed."

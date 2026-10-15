$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$mpvHeader = Get-Content (Join-Path $root "native/player/mpvitem.h") -Raw
$mpvSource = Get-Content (Join-Path $root "native/player/mpvitem.cpp") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: player needs a diagnostics overlay with mpv stats and track state.
Assert-Contains $mpvHeader "Q_INVOKABLE QVariant mpvProperty" `
    "MpvItem must expose safe mpv property reads for playback stats."
Assert-Contains $mpvSource "QVariant MpvItem::mpvProperty" `
    "MpvItem must implement mpvProperty."
Assert-Contains $mpvSource "allowedStatsProperties" `
    "MpvItem stats reads must be constrained to a safe allowlist."
Assert-Contains $mpvSource "video-bitrate" `
    "MpvItem stats allowlist must include video bitrate."
Assert-Contains $mpvSource "frame-drop-count" `
    "MpvItem stats allowlist must include dropped frame stats."

Assert-Contains $player "property bool statsOverlayOpen" `
    "PlayerPage must track stats overlay visibility."
Assert-Contains $player "property var playbackStats" `
    "PlayerPage must store playback stats."
Assert-Contains $player "function refreshPlaybackStats" `
    "PlayerPage must refresh playback stats from mpv."
Assert-Contains $player "mpv.mpvProperty" `
    "PlayerPage must call the native mpv stats bridge."
Assert-Contains $player "video-codec" `
    "PlayerPage stats must read video codec."
Assert-Contains $player "audio-codec" `
    "PlayerPage stats must read audio codec."
Assert-Contains $player "estimated-vf-fps" `
    "PlayerPage stats must read estimated FPS."
Assert-Contains $player "cache-buffering-state" `
    "PlayerPage stats must read cache buffering state."
Assert-Contains $player "Playback stats" `
    "PlayerPage must render a Playback stats overlay."
Assert-Contains $player "Video codec" `
    "Stats overlay must display video codec."
Assert-Contains $player "Audio codec" `
    "Stats overlay must display audio codec."
Assert-Contains $player "Dropped frames" `
    "Stats overlay must display dropped frames."
Assert-Contains $player "Audio track" `
    "Stats overlay must display active audio track."
Assert-Contains $player "Subtitle track" `
    "Stats overlay must display active subtitle track."
Assert-Contains $player '"label": "Playback stats", "kind": "stats"' `
    "Playback stats must live as an overflow-menu row (ToolsMenu retired 2026-07-08)."

# Async stats contract (2026-09-19 drop-burst fix): periodic stats reads must NEVER block the
# GUI thread on the mpv core. The card refreshes via one async batch; only rare one-off reads
# (pause card quality line, mpvClean) may still use the sync bridge.
Assert-Contains $mpvHeader "Q_INVOKABLE void requestPlaybackStatsAsync" `
    "MpvItem must expose the async stats batch."
Assert-Contains $mpvSource "void MpvItem::requestPlaybackStatsAsync" `
    "MpvItem must implement the async stats batch."
Assert-Contains $mpvSource "playbackStatsReady" `
    "The batch must land as one aggregated playbackStatsReady signal."
Assert-Contains $player "mpv.requestPlaybackStatsAsync" `
    "refreshPlaybackStats must dispatch the async batch, not sync reads."
Assert-Contains $player "function applyPlaybackStats" `
    "PlayerPage must consume the aggregated stats map."
Assert-Contains $player "onPlaybackStatsReady" `
    "The mpv Connections block must route playbackStatsReady into applyPlaybackStats."
Assert-Contains $player "interval: 2000" `
    "The stats card refresh cadence is 2s (halved 2026-09-19)."
if ($player -match '(?s)function refreshPlaybackStats\(\)\s*\{[^}]*mpvProperty') {
    throw "refreshPlaybackStats must not call the sync mpvProperty bridge inside its body."
}

Write-Host "Player stats P0 parity contract checks passed."

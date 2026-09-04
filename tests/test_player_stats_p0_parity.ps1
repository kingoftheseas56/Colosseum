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
Assert-Contains $player "mpv.playbackStat" `
    "PlayerPage must call the neutral playback stats bridge."
Assert-Contains $player 'playbackStat("videoCodec")' `
    "PlayerPage stats must read video codec through the neutral bridge."
Assert-Contains $player 'playbackStat("audioCodec")' `
    "PlayerPage stats must read audio codec through the neutral bridge."
Assert-Contains $player 'playbackStat("estimatedFps")' `
    "PlayerPage stats must read estimated FPS through the neutral bridge."
Assert-Contains $player 'playbackStat("bufferingPercent")' `
    "PlayerPage stats must read buffering state through the neutral bridge."
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

Write-Host "Player stats P0 parity contract checks passed."

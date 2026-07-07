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

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

Assert-Contains $mpvHeader "void playbackError(QString code, QString message);" `
    "MpvItem must emit a typed playback error."
Assert-Contains $mpvSource "mapEndFileErrorCode" `
    "MpvItem must map mpv end-file details into stable error codes."
Assert-Contains $mpvSource "QStringLiteral(`"network`")" `
    "MpvItem error contract must include network."
Assert-Contains $mpvSource "QStringLiteral(`"decode`")" `
    "MpvItem error contract must include decode."
Assert-Contains $mpvSource "QStringLiteral(`"codec`")" `
    "MpvItem error contract must include codec."
Assert-Contains $mpvSource "QStringLiteral(`"source`")" `
    "MpvItem error contract must include source."
Assert-Contains $mpvSource "QStringLiteral(`"unknown`")" `
    "MpvItem error contract must include unknown."

Assert-Contains $player "property string lastPlaybackErrorCode" `
    "PlayerPage must store the last typed playback error code."
Assert-Contains $player "property string lastPlaybackErrorMessage" `
    "PlayerPage must store the last typed playback error message."
Assert-Contains $player "function handlePlaybackIssue" `
    "PlayerPage must route typed playback issues through one handler."
Assert-Contains $player "onPlaybackError: function(code, message)" `
    "PlayerPage must listen to native playbackError."
Assert-Contains $player "root.handlePlaybackIssue(code, message)" `
    "Native playbackError must drive recovery."

Assert-Contains $player "property real positionFrozenSeconds: 18" `
    "RecoveryWatch must use an 18 second early-freeze window."
Assert-Contains $player "property real noVideoGraceSeconds: 20" `
    "RecoveryWatch must allow mpv time to report video dimensions."
Assert-Contains $player "property real wakeReconnectGapSeconds: 30" `
    "WakeReconnect must detect long system sleep/stall gaps."
Assert-Contains $player "property int wakeReconnectTickMs: 2000" `
    "WakeReconnect must tick lightly while streams are open."
Assert-Contains $player "property real positionStartedFloorSec: 5" `
    "RecoveryWatch must only auto-recover early startup freezes."
Assert-Contains $player "property real positionAdvanceEpsilonSec: 0.3" `
    "RecoveryWatch must ignore tiny position jitter."
Assert-Contains $player "function resetRecoveryWatch" `
    "PlayerPage must reset recovery state per source."
Assert-Contains $player "function tickRecoveryWatch" `
    "PlayerPage must sample position and video readiness."
Assert-Contains $player "function tickWakeReconnect" `
    "PlayerPage must reconnect a network stream after a long wake gap."
Assert-Contains $player "recoveryWatchTimer" `
    "PlayerPage must have a repeating recovery timer."
Assert-Contains $player "wakeReconnectTimer" `
    "PlayerPage must have a lightweight wake reconnect timer."
Assert-Contains $player "position frozen" `
    "RecoveryWatch must detect frozen position."
Assert-Contains $player "no video" `
    "RecoveryWatch must detect audio/clock without video frames."
Assert-Contains $player "mpv.mpvProperty(`"width`")" `
    "RecoveryWatch must use mpv width stats for no-video detection."
Assert-Contains $player "mpv.mpvProperty(`"height`")" `
    "RecoveryWatch must use mpv height stats for no-video detection."
Assert-Contains $player "root.currentPlaybackUrl.length" `
    "WakeReconnect must reload the current playback URL, not re-resolve a source blindly."
Assert-Contains $player "mpv.seekExact(pos)" `
    "WakeReconnect must restore the pre-sleep position after reload."
Assert-Matches $player "root\.mediaLocalPath\.length[\s\S]*return" `
    "Local downloaded files must be excluded from automatic source switching."
Assert-Matches $player "root\.subStreamId\.indexOf\(`"iptv:`"\)[\s\S]*return" `
    "Live playback must be excluded from automatic frozen/no-video switching."

Write-Host "Player typed recovery contract checks passed."

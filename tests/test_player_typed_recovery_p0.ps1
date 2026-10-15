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
    "MpvItem must expose playback errors to QML."
Assert-Contains $mpvSource "Q_EMIT playbackError(QStringLiteral(`"unknown`"), reason);" `
    "The current MpvQt endFile seam must report an honest generic code."
if ($mpvSource -like "*mapEndFileErrorCode*") {
    throw "MpvItem must not claim typed classification from MpvQt's coarse endFile(reason) signal."
}
if ($mpvSource -like "*QStringLiteral(`"network`")*" -or
    $mpvSource -like "*QStringLiteral(`"decode`")*" -or
    $mpvSource -like "*QStringLiteral(`"codec`")*" -or
    $mpvSource -like "*QStringLiteral(`"source`")*") {
    throw "Dead typed-error branches must not masquerade as verified runtime behavior."
}

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
Assert-Contains $player "mpv.decodedWidth" `
    "RecoveryWatch must use decoded-frame width truth for no-video detection."
Assert-Contains $player "mpv.decodedHeight" `
    "RecoveryWatch must use decoded-frame height truth for no-video detection."
Assert-Contains $player "root.currentPlaybackUrl.length" `
    "WakeReconnect must reload the current playback URL, not re-resolve a source blindly."
Assert-Contains $player "mpv.seekExact(pos)" `
    "WakeReconnect must restore the pre-sleep position after reload."
Assert-Matches $player "root\.mediaLocalPath\.length[\s\S]*return" `
    "Local downloaded files must be excluded from automatic source switching."
Assert-Matches $player "root\.subStreamId\.indexOf\(`"iptv:`"\)[\s\S]*return" `
    "Live playback must be excluded from automatic frozen/no-video switching."

Write-Host "Player typed recovery contract checks passed."

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

# Harbor parity P0: player quick tools need GIF clip recording with start/stop/abort and toast feedback.
Assert-Contains $mpvHeader "Q_INVOKABLE bool startGifRecording" `
    "MpvItem must expose GIF recording start."
Assert-Contains $mpvHeader "Q_INVOKABLE void stopGifRecording" `
    "MpvItem must expose GIF recording stop/export (async; result via gifSaved/gifFailed)."
Assert-Contains $mpvHeader "Q_INVOKABLE void abortGifRecording" `
    "MpvItem must expose GIF recording abort."
Assert-Contains $mpvSource "gifCaptureFrame" `
    "MpvItem must capture GIF frames while recording."
Assert-Contains $mpvSource "screenshot-to-file" `
    "GIF recording must use mpv screenshots as frame source."
Assert-Contains $mpvSource "ffmpeg" `
    "GIF export must use ffmpeg when available."
Assert-Contains $mpvSource ".gif" `
    "GIF export must produce .gif files."
Assert-Contains $mpvSource "QStandardPaths::PicturesLocation" `
    "GIF exports must prefer the user's Pictures folder."

Assert-Contains $player "property string gifState" `
    "PlayerPage must track GIF state."
Assert-Contains $player "property int gifElapsedSec" `
    "PlayerPage must track GIF elapsed seconds."
Assert-Contains $player "function startGifRecording" `
    "PlayerPage must expose GIF start action."
Assert-Contains $player "function stopGifRecording" `
    "PlayerPage must expose GIF stop action."
Assert-Contains $player "function abortGifRecording" `
    "PlayerPage must expose GIF abort action."
Assert-Contains $player "mpv.startGifRecording" `
    "PlayerPage must call native GIF start."
Assert-Contains $player "mpv.stopGifRecording" `
    "PlayerPage must call native GIF stop."
Assert-Contains $player "GIF saved" `
    "PlayerPage must show successful GIF feedback."
Assert-Contains $player "GIF export failed" `
    "PlayerPage must show failed GIF feedback."
Assert-Contains $player "Recording GIF" `
    "PlayerPage must show an active recording pill."
Assert-Contains $player "Record GIF" `
    "Player quick tools must expose a GIF recording action."
Assert-Contains $player "`"icon`": `"gif`"" `
    "Player quick tools must render a GIF icon."

Write-Host "Player GIF recorder P0 parity contract checks passed."

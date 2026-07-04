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

# Harbor parity P0: player quick tools need screenshot capture, toast feedback, and reveal.
Assert-Contains $mpvHeader "Q_INVOKABLE QString captureFrame" `
    "MpvItem must expose frame capture to QML."
Assert-Contains $mpvHeader "Q_INVOKABLE void revealCaptureFolder" `
    "MpvItem must expose capture folder reveal to QML."
Assert-Contains $mpvSource "screenshot-to-file" `
    "MpvItem must use mpv's screenshot-to-file command."
Assert-Contains $mpvSource "screenshot-format" `
    "MpvItem must configure screenshot format."
Assert-Contains $mpvSource "png" `
    "MpvItem must save frame grabs as PNG."
Assert-Contains $mpvSource "QStandardPaths::PicturesLocation" `
    "Frame grabs must prefer the user's Pictures folder."
Assert-Contains $mpvSource "QDesktopServices::openUrl" `
    "Frame grabs must support opening the capture folder."
Assert-Contains $mpvSource "captureBaseName" `
    "Frame grab filenames must be based on the media title/time."

Assert-Contains $player "property bool frameGrabToastOpen" `
    "PlayerPage must own frame grab toast state."
Assert-Contains $player "property string frameGrabToastText" `
    "PlayerPage must expose frame grab toast text."
Assert-Contains $player "property string frameGrabPath" `
    "PlayerPage must keep the latest frame grab path."
Assert-Contains $player "function captureFrameGrab" `
    "PlayerPage must expose a frame grab action."
Assert-Contains $player "mpv.captureFrame" `
    "PlayerPage must call native mpv.captureFrame."
Assert-Contains $player "mpv.revealCaptureFolder" `
    "PlayerPage must call native revealCaptureFolder."
Assert-Contains $player "Screenshot saved" `
    "PlayerPage must show successful frame grab feedback."
Assert-Contains $player "Frame grab failed" `
    "PlayerPage must show failed frame grab feedback."
Assert-Contains $player "Open folder" `
    "PlayerPage frame grab toast must support opening the folder."
Assert-Contains $player "icon: `"camera`"" `
    "PlayerPage transport controls must expose a frame grab action."
Assert-Contains $player "tooltip: `"Frame grab`"" `
    "PlayerPage frame grab button must be discoverable."

Write-Host "Player frame grab P0 parity contract checks passed."

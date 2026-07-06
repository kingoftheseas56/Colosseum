$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$mpvcpp = Get-Content (Join-Path $root "native/player/mpvitem.cpp") -Raw
$mpvh = Get-Content (Join-Path $root "native/player/mpvitem.h") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# GIF encode must never block the UI thread (spec 2026-07-06 slice 6; was a 60s freeze).
Assert-NotContains $mpvcpp 'waitForFinished(60000)' `
    "The blocking 60s FFmpeg wait must be gone."
Assert-Contains $mpvh 'void gifSaved(QString path)' `
    "MpvItem must announce the finished GIF via signal."
Assert-Contains $mpvh 'void gifFailed()' `
    "MpvItem must announce encode failure via signal."
Assert-Contains $player 'onGifSaved' `
    "PlayerPage must consume the async gifSaved signal."
Assert-Contains $player 'onGifFailed' `
    "PlayerPage must consume the async gifFailed signal."

Write-Host "Player async-GIF contract checks passed."

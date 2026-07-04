$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: dropping a local subtitle file on the player should load it and show feedback.
Assert-Contains $player "subtitleDropToastOpen" `
    "PlayerPage must track subtitle drop toast visibility."
Assert-Contains $player "subtitleDropToastText" `
    "PlayerPage must track subtitle drop toast text."
Assert-Contains $player "function isSubtitleFile" `
    "PlayerPage must filter dropped files to subtitle extensions."
Assert-Contains $player ".srt" `
    "Subtitle drop filter must accept .srt files."
Assert-Contains $player ".ass" `
    "Subtitle drop filter must accept .ass files."
Assert-Contains $player ".ssa" `
    "Subtitle drop filter must accept .ssa files."
Assert-Contains $player ".vtt" `
    "Subtitle drop filter must accept .vtt files."
Assert-Contains $player ".sub" `
    "Subtitle drop filter must accept .sub files."
Assert-Contains $player "function loadDroppedSubtitle" `
    "PlayerPage must route dropped subtitle files through a dedicated loader."
Assert-Contains $player "root.loadSubtitleFile" `
    "Dropped subtitle loader must reuse the existing local subtitle add path."
Assert-Contains $player "DropArea" `
    "PlayerPage must expose a drop target over the video stage."
Assert-Contains $player "onDropped" `
    "DropArea must handle dropped files."
Assert-Contains $player "Loaded " `
    "PlayerPage must show successful dropped-subtitle feedback."
Assert-Contains $player "Couldn't load " `
    "PlayerPage must show failed dropped-subtitle feedback."
Assert-Contains $player "root.userTouchedSubs = true" `
    "Dropped subtitles must disable later subtitle auto-overrides like Harbor prefs."

Write-Host "Player subtitle drop P0 parity contract checks passed."

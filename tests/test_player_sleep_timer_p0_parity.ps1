$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: speed controls include a sleep timer with minute/end-of-episode presets.
Assert-Contains $player "property string sleepTimerMode" `
    "PlayerPage must track the active sleep timer mode."
Assert-Contains $player "property real sleepTimerFiresAt" `
    "PlayerPage must track when minute-based sleep timers fire."
Assert-Contains $player "property int sleepEndEpisodesRemaining" `
    "PlayerPage must support Harbor's end-of-next-episode sleep mode."
Assert-Contains $player "readonly property bool sleepTimerActive" `
    "PlayerPage must expose active sleep timer state."
Assert-Contains $player "readonly property var sleepPresets" `
    "PlayerPage must expose Harbor sleep presets."
Assert-Contains $player "15 min" `
    "Sleep presets must include Harbor's 15 minute preset."
Assert-Contains $player "End of episode" `
    "Sleep presets must include Harbor's end-of-episode preset."
Assert-Contains $player "End of next episode" `
    "Sleep presets must include Harbor's end-of-next-episode preset."
Assert-Contains $player "function setSleepTimer" `
    "PlayerPage must expose a setSleepTimer action."
Assert-Contains $player "function cancelSleepTimer" `
    "PlayerPage must expose a cancelSleepTimer action."
Assert-Contains $player "function handleSleepEpisodeEnd" `
    "PlayerPage must handle end-of-episode sleep behavior."
Assert-Contains $player "mpv.pause = true" `
    "Sleep timer must pause playback when it fires."
Assert-Contains $player "Sleep timer" `
    "Speed menu must render a Sleep timer section."
Assert-Contains $player "Speed & sleep" `
    "Speed menu tooltip must expose combined speed and sleep behavior."
Assert-Contains $player "Cancel timer" `
    "Speed menu must provide a timer cancel action."
Assert-Contains $player "sleepTimerLabel" `
    "Speed button must show active sleep timer status."

Write-Host "Player sleep timer P0 parity contract checks passed."

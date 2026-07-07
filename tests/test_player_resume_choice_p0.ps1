$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

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

Assert-Contains $player "property real resumePromptMinSec: 30" `
    "PlayerPage must only prompt after meaningful progress."
Assert-Contains $player "property real resumeRestartThreshold: 0.80" `
    "PlayerPage must restart near-finished videos from zero."
Assert-Contains $player "property bool resumeChoiceOpen" `
    "PlayerPage must track the visible resume choice."
Assert-Contains $player "property bool resumePromptConsumed" `
    "PlayerPage must prompt only once per source load."
Assert-Contains $player "function prepareResumeChoice" `
    "PlayerPage must decide whether a saved position should prompt, seek, or restart."
Assert-Contains $player "function acceptResumeChoice" `
    "PlayerPage must expose the Resume action."
Assert-Contains $player "function startOverFromResumeChoice" `
    "PlayerPage must expose the Start over action."
Assert-Contains $player "function shouldSkipResumePrompt" `
    "PlayerPage must centralize resume exclusions."
Assert-Matches $player "function acceptResumeChoice[\s\S]*mpv\.seekExact\(root\.resumeChoiceSec\)" `
    "Accepting resume must SEEK to the saved position (onFileLoaded already fired; setting pendingSeekSec would be a dead write)."
Assert-Contains $player "root.pendingSeekSec = 0" `
    "Starting over must explicitly clear the saved seek."
Assert-Matches $player "mpv\.duration\s*>\s*0[\s\S]*root\.resumeChoiceSec\s*/\s*mpv\.duration\s*>=\s*root\.resumeRestartThreshold" `
    "Near-finished content must skip prompting and restart from zero."
Assert-Matches $player "root\.subStreamId\.indexOf\(`"iptv:`"\)\s*===\s*0|root\.mediaId\.indexOf\(`"iptv:`"\)\s*===\s*0" `
    "Live playback must be excluded from resume prompting."
Assert-Contains $player "Resume from" `
    "The overlay must show a plain Resume action."
Assert-Contains $player "Start over" `
    "The overlay must show a plain Start over action."

Write-Host "Player resume choice contract checks passed."

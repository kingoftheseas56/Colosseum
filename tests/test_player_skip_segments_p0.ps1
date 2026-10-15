$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$skipPath = Join-Path $root "qml/SkipSegments.js"
$playerPath = Join-Path $root "qml/PlayerPage.qml"

if (!(Test-Path $skipPath)) {
    throw "SkipSegments.js must exist."
}

$skip = Get-Content $skipPath -Raw
$player = Get-Content $playerPath -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

Assert-Contains $skip "function chaptersToSegments" `
    "SkipSegments must derive segments from mpv chapters."
Assert-Contains $skip "function parseAniSkipResults" `
    "SkipSegments must parse AniSkip results."
Assert-Contains $skip "function sanitizeSegments" `
    "SkipSegments must sanitize provider ranges."
Assert-Contains $skip "function mergeSegments" `
    "SkipSegments must merge providers instead of selecting only one."
Assert-Contains $skip "function activeSegment" `
    "SkipSegments must identify the segment active at the current position."
Assert-Contains $skip "opening|op|intro|opening credits|theme song" `
    "Chapter classifier must recognize intro names."
Assert-Contains $skip "ending|ed|outro|credits" `
    "Chapter classifier must recognize outro names."
Assert-Contains $skip "recap|previously" `
    "Chapter classifier must recognize recap names."
Assert-Contains $skip "MIN_SEGMENT_SEC = 2" `
    "Sanitizer must reject tiny ranges."
Assert-Contains $skip "MAX_SEGMENT_SEC = 360" `
    "Sanitizer must reject huge ranges."
Assert-Contains $skip "MIN_OUTRO_START_FRACTION = 0.5" `
    "Sanitizer must reject early outros."
Assert-Contains $skip "sourceRank" `
    "Merge must prefer better overlapping sources without dropping all lower-source data."

Assert-Contains $player 'import "SkipSegments.js" as SkipSegments' `
    "PlayerPage must import SkipSegments."
Assert-Contains $player "property var skipSegments" `
    "PlayerPage must store active skip segments."
Assert-Contains $player "property string skipDiagnostics" `
    "PlayerPage must expose diagnostic status for provider failures."
Assert-Contains $player "property real skipSafetyOffsetSec: 0.75" `
    "Skip action must seek past the segment end with a safety offset."
Assert-Contains $player "property bool showSkipButton: true" `
    "Skip button should be visible by default."
Assert-Contains $player "property bool autoSkipIntro: false" `
    "Auto-skip intro must default off."
Assert-Contains $player "property bool autoSkipRecap: false" `
    "Auto-skip recap must default off."
Assert-Contains $player "property bool autoSkipCredits: false" `
    "Auto-skip credits must default off."
Assert-Contains $player "function loadSkipSegments" `
    "PlayerPage must load skip segments per media."
Assert-Contains $player "function performSegmentSkip" `
    "PlayerPage must perform a safe segment skip."
Assert-Contains $player "skip.endSec + root.skipSafetyOffsetSec" `
    "Skip seek must include the safety offset."
Assert-Contains $player "function currentSkipSegment" `
    "PlayerPage must compute the currently active segment."
Assert-Contains $player "Skip Intro" `
    "Skip pill must label intro ranges plainly."
Assert-Contains $player "Skip Recap" `
    "Skip pill must label recap ranges plainly."
Assert-Contains $player "Skip Credits" `
    "Skip pill must label outro ranges plainly."
Assert-Matches $player "root\.subStreamId\.indexOf\(`"iptv:`"\)[\s\S]*return" `
    "Live playback must be excluded."
Assert-Contains $player "root.upNextVisible" `
    "Existing Up Next must remain the final-episode card and win over skip pill."

Write-Host "Player skip segment contract checks passed."

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "qml/PlayerTrackPrefs.js"
if (!(Test-Path $path)) { throw "PlayerTrackPrefs.js must exist." }
$text = Get-Content $path -Raw

function Assert-Contains($text, $needle, $message) {
    # Literal substring match (see track-language test): -like mis-parses bracket needles.
    if (-not $text.Contains($needle)) { throw $message }
}

Assert-Contains $text "var MAX_PREFS = 200" "Preference store must cap at 200 records."
Assert-Contains $text "function readStore" "Preference store must parse JSON safely."
Assert-Contains $text "function getPref" "Preference store must read a show preference."
Assert-Contains $text "function upsertPref" "Preference store must upsert a show preference."
Assert-Contains $text "audioLang" "Preference records must support audioLang."
Assert-Contains $text "subtitleLang" "Preference records must support subtitleLang."
Assert-Contains $text "subtitlesOff" "Preference records must support subtitlesOff."
Assert-Contains $text "audioDelay" "Preference records must support audioDelay."
Assert-Contains $text "subDelay" "Preference records must support subDelay."
Assert-Contains $text "updatedAt" "Preference records must store recency."
Assert-Contains $text "slice(0, MAX_PREFS)" "Preference store must trim old records."

Write-Host "Player track preference contract checks passed."

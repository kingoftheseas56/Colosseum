$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "qml/TrackLanguage.js"
if (!(Test-Path $path)) { throw "TrackLanguage.js must exist." }
$text = Get-Content $path -Raw

function Assert-Contains($text, $needle, $message) {
    # Literal substring match: -like would treat needle brackets (e.g. parts[0]) as
    # wildcard character-classes and never match the literal code.
    if (-not $text.Contains($needle)) { throw $message }
}

Assert-Contains $text "function normalizeLang" "TrackLanguage must normalize language codes."
Assert-Contains $text "eng: `"eng`"" "English aliases must normalize to eng."
Assert-Contains $text "jpn: `"jpn`"" "Japanese aliases must normalize to jpn."
Assert-Contains $text "function parseLanguageList" "TrackLanguage must parse comma language settings."
Assert-Contains $text "function showKey" "TrackLanguage must derive per-show preference keys."
Assert-Contains $text "return kind + `":`" + parts[0]" "Cinemeta episodes must group by base tt id."
Assert-Contains $text "return kind + `":`" + parts[0] + `":`" + parts[1]" "Anime ids must group by provider plus series id."
Assert-Contains $text "function trackSignature" "Automation must use a focused track-set signature."
Assert-Contains $text "function filterBlockedTracks" "TrackLanguage must filter blocked words."
Assert-Contains $text "if (out.length === 0) return tracks" "Blocked-word filtering must fall back if every track would be removed."
Assert-Contains $text "function pickBestAudioTrack" "TrackLanguage must pick audio tracks."
Assert-Contains $text "function pickBestSubtitleTrack" "TrackLanguage must pick subtitle tracks."
Assert-Contains $text "preferEmbeddedSubtitles" "Subtitle ranking must support embedded preference."
Assert-Contains $text "forcedOnly" "Subtitle ranking must support forced-only mode."
Assert-Contains $text "subtitleAutoUpgrade" "Policy signature must account for subtitle auto-upgrade."

Write-Host "Player track language contract checks passed."

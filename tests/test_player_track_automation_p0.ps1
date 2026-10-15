$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$menu = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    # Literal substring match: -like mis-parses bracket needles as wildcard classes.
    if (-not $text.Contains($needle)) { throw $message }
}
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

Assert-Contains $player 'import "TrackLanguage.js" as TrackLanguage' "PlayerPage must import TrackLanguage."
Assert-Contains $player 'import "PlayerTrackPrefs.js" as PlayerTrackPrefs' "PlayerPage must import PlayerTrackPrefs."
foreach ($needle in @(
    'property string preferredAudioLanguages: "eng,jpn"',
    'property string preferredSubtitleLanguages: "eng"',
    'property string blockedTrackWords: "commentary"',
    'property bool preferEmbeddedSubtitles: false',
    'property bool subtitleAutoUpgrade: false',
    'property bool forcedSubsWhenNativeAudio: false',
    'property bool subtitlesOffByDefault: false',
    'property string trackPrefsJson: "{}"'
)) {
    Assert-Contains $player $needle "Missing player setting: $needle"
}
Assert-Contains $player "property bool   userTouchedAudio" "PlayerPage must lock audio after manual selection."
Assert-Contains $player "property string trackAutoDoneKey" "PlayerPage must remember first auto subtitle selection."
Assert-Contains $player "property string autoAudioTrackId" "PlayerPage must remember automatic audio id."
Assert-Contains $player "property string autoSubtitleTrackId" "PlayerPage must remember automatic subtitle id."
Assert-Contains $player "function currentShowKey" "PlayerPage must derive a per-show key."
Assert-Contains $player "function currentTrackPreference" "PlayerPage must read per-show preferences."
Assert-Contains $player "function saveTrackPreference" "PlayerPage must save per-show preferences."
Assert-Contains $player "function trackAutomationExcluded" "PlayerPage must exclude live playback."
Assert-Matches $player "trackAutomationExcluded[\s\S]*iptv:" "Track automation must exclude iptv live playback."
Assert-Contains $player "function trackPolicyKey" "PlayerPage must build a focused policy key."
Assert-Contains $player "function maybeAutoSelectTracks" "PlayerPage must implement auto-selection."
Assert-Contains $player "TrackLanguage.trackSignature" "Auto-selection must use track-set signature."
Assert-Contains $player "TrackLanguage.pickBestAudioTrack" "Auto-selection must rank audio tracks."
Assert-Contains $player "TrackLanguage.pickBestSubtitleTrack" "Auto-selection must rank subtitle tracks."
Assert-Contains $player "PlayerTrackPrefs.upsertPref" "Manual choices must persist per show."
Assert-Contains $player "applySavedTrackDelays" "Saved per-show audio/subtitle delays must be reapplied."
Assert-Contains $player "onTrackListChanged: root.maybeAutoSelectTracks" "Automation must rerun on async mpv track-list changes."
Assert-Contains $player 'root.maybeAutoSelectTracks("online-subs")' "Automation must rerun after online subtitle results settle."
Assert-Contains $player "function pickAudioTrack" "Audio picks must go through a lock/save wrapper."
Assert-Contains $player "function turnSubtitlesOff" "Subtitle Off must go through a lock/save wrapper."
Assert-Contains $player "function adjustAudioDelay" "Audio delay changes must persist per show."
Assert-Contains $player "function adjustSubtitleDelay" "Subtitle delay changes must persist per show."
Assert-Contains $player "onTrackPicked: function(trackId) { root.pickAudioTrack(trackId) }" "Audio menu must use pickAudioTrack."
Assert-Contains $player "onOffPicked: root.turnSubtitlesOff()" "Subtitle menu Off must use turnSubtitlesOff."
Assert-Contains $player "forcedSubsWhenNativeAudio" "Forced subtitle policy must be explicit and default off."
Assert-Contains $player "subtitleAutoUpgrade" "Subtitle auto-upgrade policy must be explicit and default off."

Assert-Contains $menu "property string autoStatusText" "SubtitleMenu must accept an automation status line."
Assert-Contains $menu "property bool showAutoStatus" "SubtitleMenu must allow the status line to be hidden."

Write-Host "Player track automation contract checks passed."

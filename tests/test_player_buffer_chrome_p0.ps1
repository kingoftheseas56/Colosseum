$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Clean loading screen (Hemanth 2026-07-08): while a stream is LOADING (starting), the
# control chrome must step aside so the centered spinner+status doesn't stack on top of
# the centered transport buttons ("the vomit"). The chrome's own visibility gates on
# !root.starting; the loading overlay stays. Error state keeps the chrome (retry lives
# in the control bar), so the gate is `starting`, not `starting || errored`.
Assert-Contains $player "root.controlsShown && !root.starting" `
    "Chrome must hide while a stream is loading (starting) so the loading overlay owns the screen."
Assert-Contains $player 'text: root.loadingStatusText()' `
    "Loading card must show a stable status line such as Starting stream... beneath the title."
Assert-Contains $player 'function finishStartingIfPlaybackAdvanced()' `
    "PlayerPage must clear the loading title card when playback position advances, not only on file-loaded."
Assert-Contains $player 'onPositionChanged: root.finishStartingIfPlaybackAdvanced()' `
    "mpv position advance must dismiss the loading card once audio/video playback has actually begun."
if ($player -like '*visible: text.length > 0 && (root.errored || root.statusMsg !== "Buffering...")*') {
    throw "Loading card must not suppress the buffering/starting line while a stream is still opening."
}

Write-Host "Player buffer-chrome contract checks passed."

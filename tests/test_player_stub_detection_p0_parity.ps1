$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: tiny fake/stub streams must be detected, skipped, and not retried forever.
Assert-Contains $player "property var deadStreamKeys" `
    "PlayerPage must track streams marked bad during playback."
Assert-Contains $player "property string stubCheckedKey" `
    "PlayerPage must avoid repeatedly flagging the same stub stream."
Assert-Contains $player "property int stubDurationThresholdSec: 60" `
    "PlayerPage must use Harbor's sub-60s stub threshold."
Assert-Contains $player "function streamCandidateKey" `
    "PlayerPage must derive stable stream keys from info hash and file index."
Assert-Contains $player "function markStreamDead" `
    "PlayerPage must mark detected stub streams as dead."
Assert-Contains $player "function isStreamDead" `
    "PlayerPage must skip streams already marked dead."
Assert-Contains $player "function nextPlayableStreamIndex" `
    "PlayerPage must find the next non-dead candidate."
Assert-Contains $player "function detectStubStream" `
    "PlayerPage must detect Harbor-style short fake/stub streams."
Assert-Contains $player "mpv.duration < root.stubDurationThresholdSec" `
    "Stub detection must flag durations under 60 seconds."
Assert-Contains $player "root.markStreamDead" `
    "Stub detection must mark the current stream dead."
Assert-Contains $player "stub_" `
    "Stub detection must record a stub reason."
Assert-Contains $player "Trying another stream" `
    "Stub detection must communicate automatic fallback."
Assert-Contains $player "onDurationChanged: root.detectStubStream()" `
    "PlayerPage must check stub state when mpv duration becomes known."
Assert-Contains $player "onPauseChanged: {" `
    "PlayerPage must check stub state when playback becomes active."
Assert-Contains $player "root.detectStubStream()" `
    "PlayerPage playback lifecycle must invoke stub detection."

Write-Host "Player stub detection P0 parity contract checks passed."

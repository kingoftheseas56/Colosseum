# Player first-open must not synchronously construct PlayerPage on the caller's GUI event.
# The Loader may incubate only on first activation; once loaded it stays alive for warm resume.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Isolate($text, $signature, $what) {
    $m = [regex]::Match($text, [regex]::Escape($signature) + '[\s\S]*?\n    \}')
    if (-not $m.Success) { throw "Could not isolate $what." }
    return $m.Value
}

$loader = [regex]::Match($main, 'Loader \{\s*id: playerLayer[\s\S]*?\n    \}').Value
if (-not $loader) { throw "Could not isolate playerLayer Loader." }
Assert-Contains $loader 'asynchronous: true' "PlayerPage must incubate asynchronously on first activation."
Assert-Contains $loader 'Qt.callLater(win.flushPendingPlayerOpen)' "onLoaded must flush queued playback on a later event-loop turn."

Assert-Contains $main 'property var pendingPlayerOpen: null' "Shell must own one pending first-open request."
Assert-Contains $main 'function queuePlayerSessionOpen(sessionId)' "Session opens must be queueable while PlayerPage incubates."
Assert-Contains $main 'function flushPendingPlayerOpen()' "Queued first-open work must flush only after Loader readiness."

$activate = Isolate $main 'function activateSession(rec) {' 'activateSession'
Assert-Contains $activate 'queuePlayerSessionOpen(rec.id)' "Movie session activation must queue instead of dereferencing a null Loader item."

$legacy = Isolate $main 'function openPlayer(' 'openPlayer'
Assert-Contains $legacy 'queueDirectPlayerOpen' "Legacy direct player opens must also tolerate async Loader readiness."
Write-Host "Player async first-open contract checks passed."

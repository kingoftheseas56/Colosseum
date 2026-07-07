# Minimizing the player must PAUSE but KEEP THE STREAM ALIVE, so reopening from the
# taskbar resumes in place with NO re-stream / torrent reload.
# Hemanth 2026-07-07 (option a): "don't close the player, don't re-load the torrent,
# it should pause, like normal Windows players when minimized."

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$main   = Get-Content (Join-Path $root "qml/Main.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}
function Isolate($text, $signature, $what) {
    $m = [regex]::Match($text, [regex]::Escape($signature) + '[\s\S]*?\n    \}')
    if (-not $m.Success) { throw "Could not isolate $what." }
    return $m.Value
}

# --- the shell tracks which movie session is warm behind the taskbar ---
Assert-Contains $main 'warmPlayerSessionId' "Main must track the warm minimized-but-alive movie session id."

# --- teardown on minimize must NOT stop the movie stream; it keeps it warm ---
$td = Isolate $main 'function teardownSession(rec) {' 'teardownSession'
Assert-NotContains $td 'playerLayer.item.stop()' "Minimize teardown must NOT stop the movie stream - it stays warm for instant resume."
Assert-Contains $td 'suspendForMinimize' "Minimize teardown must suspend pause-plus-keep-alive the movie player."
Assert-Contains $td 'warmPlayerSessionId = rec.id' "Minimize teardown must mark the movie session warm."

# --- reopening a warm session must RESUME in place, not re-stream ---
$as = Isolate $main 'function activateSession(rec) {' 'activateSession'
Assert-Contains $as 'warmPlayerSessionId === rec.id' "Activate must detect a warm player and resume instead of re-streaming."
Assert-Contains $as 'resumeFromMinimize' "Warm resume must call resumeFromMinimize with no playTorrent reload."

# --- a real CLOSE must still end the stream: only minimize keeps it warm ---
$cs = Isolate $main 'function closeSession(id) {' 'closeSession'
Assert-Contains $cs 'playerLayer.item.stop()' "Closing a session must truly stop the stream; minimize keeps warm, close does not."

# --- player exposes the suspend/resume pair the warm lifecycle needs ---
Assert-Contains $player 'function suspendForMinimize' "Player must expose suspendForMinimize pause-plus-keep-alive."
Assert-Contains $player 'function resumeFromMinimize' "Player must expose resumeFromMinimize unpause a warm session, no reload."

Write-Host "Player minimize keep-alive contract checks passed."

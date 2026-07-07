$ErrorActionPreference = "Stop"

# Regression guard: a STREAMED (torrent) Continue-Watching item must resume at its saved
# position, not restart from zero. The tile shows position from the ProgressStore, but the
# resume path reads it from the session — so the saved position must be THREADED from
# resumeContinue -> openMovieSession -> target.position -> applied on first-open.
# Root cause (2026-07-07): the stream branch dropped r.position; only the local branch passed it.

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

Assert-Matches $main "win\.openMovieSession\([^\r\n]*r\.position" `
    "resumeContinue must pass the saved position (r.position) into openMovieSession for streamed items."
Assert-Matches $main "function openMovieSession\([^\)]*position" `
    "openMovieSession must accept a position parameter."
Assert-Matches $main '"position":\s*position' `
    "openMovieSession must carry position into the session target."
Assert-Matches $main "Number\(t\.position\)" `
    "activateSession must read the streamed target's saved position for first-open resume."
Assert-Matches $main "restoreState\(resumeSt\)" `
    "activateSession must apply target.position (with a fresher captured savedState winning) via restoreState."

Write-Host "Continue-Watching streamed resume contract checks passed."

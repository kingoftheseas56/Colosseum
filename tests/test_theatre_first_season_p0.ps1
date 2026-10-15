# Opening a fresh show lands on the FIRST numbered season, not the latest
# (Hemanth 2026-07-20). Seasons are numbered-ascending with Specials (0) pinned
# last, so the fresh-show fallback in defaultSeason() must iterate FORWARD and
# return the first entry > 0 - never scan backward from the end (that was the bug).
# The saved-season and recent-progress branches above it are untouched: resuming a
# show still returns to where you left off.

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$series = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw

function Assert-Contains($text, $needle, $message) { if (-not $text.Contains($needle)) { throw $message } }
function Assert-Absent($text, $needle, $message) { if ($text.Contains($needle)) { throw $message } }

# Fresh-show fallback iterates forward from the first season.
Assert-Contains $series 'for (var i = 0; i < seasons.length; i++)' "defaultSeason must scan seasons forward (first numbered), not backward (latest)."

# The old latest-season scan must be gone.
Assert-Absent $series 'for (var i = seasons.length - 1; i >= 0; i--)' "The backward (latest-season) scan must not return - it lands on the final season."

# Resume still honored: saved + recent-progress branches remain.
Assert-Contains $series 'Progress.lastSeason(currentId())' "Saved-season resume must remain (opening mid-watch returns to your season)."
Assert-Contains $series 'recentProgressSeason()' "Recent-progress season resume must remain."

Write-Host "PASS - fresh shows open on the first season, resume still honored."

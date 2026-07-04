$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Issue 3: clicking an anime Continue tile (Jikan id "mal:<id>", resolved via the Kitsu addon)
# must open the SAME series view shows/movies use (TheatreSeries) — NOT the universe landing page.

Assert-Contains $main "mal|kitsu|anilist|anidb" `
    "detailContinue must recognise anime addon ids (mal/kitsu/anilist/anidb)."
# NB: PowerShell -like treats [ ] as wildcards, so match a bracket-free slice of the call.
Assert-Contains $main "win.openTheatreSeries({ id: p" `
    "An anime Continue tile must open its series view via the reconstructed PREFIX:NUM series id."

# The wrong universe-page routing (and its dead helper) must be gone.
Assert-NotContains $main "win.openUniverse(title)" `
    "Anime Continue must NOT open the universe landing page."
Assert-NotContains $main "function hasUniverse" `
    "The dead hasUniverse helper from the wrong approach must be removed."

Write-Host "Theatre anime Continue routing contract checks passed."

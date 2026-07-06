$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Cast backend is manual URL sharing — the UI must say so (spec 2026-07-06 slice 1).
Assert-Contains $player '"label": "Share stream"' `
    "Tools action must be 'Share stream', not 'Cast to device'."
Assert-NotContains $player 'Cast to device' `
    "'Cast to device' label must be gone (backend is URL sharing)."
Assert-NotContains $player 'Cast to TV or speaker' `
    "Cast panel title must not promise TV/speaker control."
Assert-Contains $player 'Share this stream' `
    "Cast panel must carry the honest 'Share this stream' title."
Assert-NotContains $player 'No Chromecast, DLNA, or Roku devices found' `
    "Empty-state must not name receiver tech the backend cannot drive."

# Watch room backend is local-only — the panel must say so.
Assert-Contains $player 'Local preview' `
    "Room panel must badge itself 'Local preview'."
Assert-Contains $player 'comes later' `
    "Room panel must state multi-device rooms come later."

Write-Host "Player honest-labels contract checks passed."

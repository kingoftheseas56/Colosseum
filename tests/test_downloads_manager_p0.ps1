$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$facade = Get-Content (Join-Path $root "native/engine/LocalDownloads.cpp") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotMatches($text, $pattern, $message) {
    if ($text -match $pattern) { throw $message }
}

# ── Slice 0: exact-row discipline ──
# Remove/Cancel on a theatre row must act on THAT row's id, never on the active job.
Assert-Contains $facade 'm_videos->cancelJob(id)' `
    "LocalDownloads::cancel theatre branch must call cancelJob(id) - exact-row discipline."
Assert-NotMatches $facade 'm_videos->cancelDownload\(\)' `
    "LocalDownloads must never call the no-arg cancelDownload() - it kills the ACTIVE job regardless of which row was clicked."

Write-Host "downloads manager p0 contract: OK"

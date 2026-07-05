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

$storeHeader = Get-Content (Join-Path $root "native/player/downloadstore.h") -Raw
$storeSource = Get-Content (Join-Path $root "native/player/downloadstore.cpp") -Raw
$mainCpp = Get-Content (Join-Path $root "native/main.cpp") -Raw

Assert-Contains $storeHeader 'void selfTest(const QString &mode)' `
    "DownloadStore must carry the videoq selftest harness."
Assert-Contains $mainCpp 'COLOSSEUM_VIDEOQ_SELFTEST' `
    "main.cpp must wire the videoq selftest env var."

Write-Host "downloads manager p0 contract: OK"

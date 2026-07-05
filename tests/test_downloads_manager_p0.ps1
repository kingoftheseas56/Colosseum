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

Assert-Contains $storeSource 'groupKeyFor' `
    "DownloadStore must stamp a shared groupKey per checkout (season = a view of the queue)."

Assert-Contains $storeSource 'void DownloadStore::sampleProgress' `
    "DownloadStore must sample per-job speed/ETA (Tankorent-grade live detail)."

Assert-Contains $storeSource 'pruneGroupIfSettled' `
    "Done rows must linger with their group, then prune together."

Assert-Contains $storeHeader 'void pauseJob(const QString &id)' `
    "Engine must support pause (keeps .part, frees the cap slot)."
Assert-Contains $storeSource 'QByteArrayLiteral("Range")' `
    "Resume must append mid-file via HTTP Range when the session url survives."

Assert-Contains $facade 'toMap().value(QStringLiteral("state")).toString()' `
    "totals.active must count live rows only (done rows never inflate the badge)."

Assert-Contains $facade '"groupKey"), j.value(QStringLiteral("groupKey"))' `
    "activeJobs must pass groupKey through - the page folds by it."
Assert-Contains $facade '"etaSec"), j.value(QStringLiteral("etaSec"))' `
    "activeJobs must pass speed/etaSec through - Tankorent-grade live detail."

$page = Get-Content (Join-Path $root "qml/DownloadsPage.qml") -Raw
Assert-Contains $page 'function groupJobs' `
    "DownloadsPage must fold jobs into checkout groups (season = one collapsible row)."
Assert-Contains $page 'Cancel season' `
    "Group header must offer season-wide cancel."
Assert-NotMatches $page 'LocalDownloads\.cancel\(jobCard' `
    "The old flat job-card strip must be gone."

Assert-Contains $page 'SEASON " + sgrp.modelData.season' `
    "Theatre ledger must fold episodes under collapsible season headers."
Assert-Contains $page 'still arriving above' `
    "Ledger season headers must cross-reference live checkouts."

Write-Host "downloads manager p0 contract: OK"

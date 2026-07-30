# Tankoban migration - P0 contracts.
#
# WHAT THIS PINS. The migration of 2026-07-30 turned tankoban mode from an Off/On toggle over
# gap-ridden MangaDex volume data into a permanent, data-derived surface over a Comick-sourced
# volume DB behind a completeness gate. Seven properties came out of that change, and every one
# of them is the kind a later refactor restores by accident:
#   1. MangaDex is retired from the runtime - the client class and the catalog API host are gone.
#   2. MangaEngine::volumes() takes the WeebCentral series id FIRST (the volume DB's key). Drop
#      it and the DB path becomes dead code: every series silently falls through to a live scrape.
#   3. QML actually passes that id, rather than the title twice.
#   4. MangaVolumes.js does no interpolation and no anchor-repair. ESTIMATED VOLUME BOUNDARIES
#      ARE REJECTED DOCTRINE - a guessed boundary is worse than a plain chapter list, because it
#      looks authoritative. The gate makes a partial range impossible, so the machinery would be
#      unreachable guessing code waiting for someone to feed it sparse data.
#   5. The toggle is gone. The data IS the verdict.
#   6. The completeness gate is enforced on BOTH native paths - the grouper (live scrape) and the
#      client (published DB record). One path without it re-admits gap-ridden shelves.
#   7. tankobanMode is derived from volumes.length, never stored - so it can never disagree with
#      the volume list it describes.
#
# WHAT IT IS NOT. This is grep shape, not behaviour: a green run proves the strings are present,
# never that the shelf renders. Behaviour lives in comick_volume_grouper_harness and
# comick_catalog_parse_harness; the final pixels are Hemanth's eyes-on.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_tankoban_migration_p0.ps1
#
# This file is deliberately pure ASCII: PowerShell 5.1 reading a BOM-less UTF-8 script mis-frames
# multi-byte characters inside quoted strings and reports bogus parse errors.

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fail = 0

function Check([string]$name, [bool]$ok) {
    if ($ok) { Write-Host "PASS  $name" }
    else { Write-Host "FAIL  $name"; $script:fail++ }
}

function Read-RepoFile([string]$rel) {
    $p = Join-Path $repo $rel
    if (-not (Test-Path -LiteralPath $p)) { throw "MISSING FILE: $rel" }
    return (Get-Content -Raw -LiteralPath $p)
}

# Every hand-written runtime source under native/ and qml/. native/build-msvc is EXCLUDED on
# purpose: it holds generated artefacts (moc output, .obj, ninja logs) that still carry the
# deleted class's name until someone wipes the build dir, and a stale artefact must not be able
# to fail a source contract.
$sources = Get-ChildItem -Path (Join-Path $repo "native"), (Join-Path $repo "qml") -Recurse -File `
    -Include *.h, *.cpp, *.qml, *.js |
    Where-Object { $_.FullName -notlike "*\build-msvc\*" }
if ($sources.Count -lt 50) { throw "source sweep found only $($sources.Count) files - the glob is broken, not the tree" }

# --- 1. MangaDex is retired from the runtime -------------------------------------------------
# uploads.mangadex.org is DELIBERATELY still allowed: qml/Catalog.js serves the Monster tile's
# cover image from that host and main.cpp keeps a live DNS pin for it. Only the catalog API host
# (api.mangadex.org) and the deleted client class are forbidden.
$dex = $sources | Select-String -Pattern "MangaDexCatalogClient|api\.mangadex\.org"
Check "no MangaDex client or catalog API host in native/ or qml/" ($null -eq $dex)
if ($null -ne $dex) { $dex | ForEach-Object { Write-Host ("      {0}:{1}: {2}" -f $_.Filename, $_.LineNumber, $_.Line.Trim()) } }

# --- 2. The engine exposes the two-argument volume fetch -------------------------------------
$engine = Read-RepoFile "native/MangaEngine.h"
Check "MangaEngine.h declares volumes(seriesId, title)" `
    ($engine.Contains('void volumes(const QString& seriesId, const QString& title)'))

# --- 3. QML fires it with the WeebCentral id, not the title twice ----------------------------
$series = Read-RepoFile "qml/MangaSeries.qml"
Check "MangaSeries fires Manga.volumes(r.id, r.title)" ($series.Contains('Manga.volumes(r.id, r.title)'))

# --- 4. No interpolation / anchor-repair machinery in MangaVolumes.js ------------------------
# Code tokens only. The file's COMMENTS legitimately say "no interpolation", so a word-level
# grep for "interpolat" would fail on the very note that documents the doctrine.
$vols = Read-RepoFile "qml/MangaVolumes.js"
$interp = @(@('runMax', 'rawEnd', 'start = null') | Where-Object { $vols.Contains($_) })
Check "MangaVolumes.js carries no interpolation or anchor-repair" ($interp.Count -eq 0)
if ($interp.Count -gt 0) { Write-Host ("      surviving tokens: {0}" -f ($interp -join ', ')) }

# --- 5. The Off/On toggle is gone ------------------------------------------------------------
$toggle = @(@('_setTankobanMode', 'setModeEnabled', 'TANKOBAN MODE') | Where-Object { $series.Contains($_) })
Check "tankoban toggle deleted from MangaSeries.qml" ($toggle.Count -eq 0)
if ($toggle.Count -gt 0) { Write-Host ("      surviving toggle strings: {0}" -f ($toggle -join ', ')) }

# --- 6. The completeness gate is enforced on both native paths -------------------------------
$grouper = Read-RepoFile "native/engine/ComickVolumeGrouper.cpp"
$client  = Read-RepoFile "native/engine/ComickCatalogClient.cpp"
Check "grouper rejects a gapped run (gap after volume)" ($grouper.Contains('gap after volume'))
Check "client re-gates its own volume list (gateVolumes)" ($client.Contains('gateVolumes'))

# --- 7. Tankoban mode is derived from the data, never stored ---------------------------------
Check "tankobanMode is derived from volumes.length" `
    ($series.Contains('property bool tankobanMode: volumes.length > 0'))

if ($fail -gt 0) {
    Write-Host ""
    Write-Host "$fail CONTRACT FAILURE(S) - tankoban migration"
    exit $fail
}
Write-Host ""
Write-Host "tankoban migration p0: ALL CONTRACTS GREEN"
exit 0

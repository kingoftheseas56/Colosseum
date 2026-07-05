$ErrorActionPreference = "Stop"

# Continue-tile unification contract (spec: haven docs/superpowers/specs/
# 2026-07-05-colosseum-continue-tiles-design.md). One ContinueTile, two variants;
# in-tile metadata; reliable posters; remove + watched wired; Tankoban blended/capped.

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- the component ---
$ct = Read-File "qml/ContinueTile.qml"
foreach ($n in @('variant: "world"', '"home"', 'signal resumeRequested()', 'signal detailRequested()',
                 'signal removeRequested()', 'entry.watched === true', 'Math.max(0, Math.min(1,')) {
    Assert-Contains $ct $n "ContinueTile must carry: $n"
}
Assert-Contains $ct 'status === Image.Error && !retried' "ContinueTile cover must retry once on error (reliable-posters call)."
Assert-Contains $ct 'kind === "manga"' "AniList fallback must be manga-gated."
Assert-Lacks $ct 'kind === "manga" || kind === "comic"' "Comics must NOT be searched on AniList as manga (audit fix)."
Assert-Contains $ct 'Remove from Continue' "Remove control must announce itself."

# --- Home migrated, inline card dead ---
$main = Read-File "qml/Main.qml"
Assert-Lacks $main 'component ContinueCard' "Main.qml must not keep the inline ContinueCard (ContinueTile owns it)."
Assert-Contains $main 'variant: "home"' "Home Continue row must use the ContinueTile home variant."
Assert-Contains $main 'onRemoveRequested: Progress.forget' "Home tiles must wire remove to Progress.forget."
Assert-Contains $main 'e.watched !== true' "Home row must sink watched entries below unfinished."

# --- world row migrated, metadata in-tile, honest header ---
$cr = Read-File "qml/ContinueRow.qml"
Assert-Contains $cr 'ContinueTile {' "ContinueRow must delegate to ContinueTile."
Assert-Contains $cr 'navigable: false' "ContinueRow header must not show the unwired chevron."
Assert-Contains $cr 'Progress.forget' "ContinueRow must wire remove to Progress.forget."
Assert-Lacks $cr 'PortraitTile' "ContinueRow must not hand-build tiles from PortraitTile anymore."
Assert-Lacks $cr 'ContinueCovers' "Cover fallback lives inside ContinueTile now."

# --- world ordering + headers ---
$tk = Read-File "qml/TankobanWorld.qml"
Assert-Contains $tk '"Continue Reading"' "Tankoban row must be titled Continue Reading."
Assert-Contains $tk 'a.sort(function(x, y) { return (y.updatedAt || 0) - (x.updatedAt || 0) })' "Tankoban must blend manga+comics by recency."
Assert-Contains $tk 'a.slice(0, 12)' "Tankoban row must be capped at 12."
Assert-Contains (Read-File "qml/BiblioWorld.qml") '"Continue Reading"' "Biblio row must be titled Continue Reading."
Assert-Contains (Read-File "qml/TheatreWorld.qml") '"Continue Watching"' "Theatre keeps Continue Watching."

Write-Host "Continue-tile unification contract checks passed."

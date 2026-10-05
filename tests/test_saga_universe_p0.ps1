$ErrorActionPreference = "Stop"

# Saga universe template contract (Hemanth correction 2026-07-12): book-first IPs get their
# OWN template — curated canon, Read = book one via Biblio, Watch = film one. Since
# 2026-10-04 an anime universe rides the same template with the read form in manga (whole
# works only, Tankoban by curated query) — still curated canon, never a name search.
# The behavioral truth rides the canon harness (exit code); this runs it then greps shape.

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

$harness = Join-Path $PSScriptRoot "saga_canon_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "saga canon harness failed (exit $LASTEXITCODE)" }

# the saga GRID (Arc 58): pairing -> rows for every archived saga, and the keyboard walk
$gridHarness = Join-Path $PSScriptRoot "saga_grid_harness.qml"
cmd /c "`"$qmlExe`" -platform offscreen `"$gridHarness`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "saga grid harness failed (exit $LASTEXITCODE)" }

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

# --- the saga page: books are the spine; a manga universe rides the SAME curated law ---
$sp = Read-File "qml/SagaUniversePage.qml"
Assert-Contains $sp 'signal bookRequested(var book)' "Saga page must carry the book verb."
Assert-Contains $sp 'root.openWork(root.readTarget)' "Read must resume the reader's book, or start at book ONE."
Assert-Contains $sp 'SagaApi.js' "Saga page must ride SagaApi (curated canon, not name-search)."
# The old blanket word-ban ('manga' must not appear) died 2026-10-04 when the story grid
# learned the manga read form (medium 'manga', kindLabel MANGA, Opens in Tankoban). The
# REAL rule it guarded survives: the page and the loader never SEARCH a manga provider —
# manga identities are pinned curation (covers baked as URLs in Universes.js), so no
# AniList, no Jikan, anywhere in the saga template's own code.
Assert-Lacks $sp 'AniList' "AniList must not be a saga source."
Assert-Lacks $sp 'Jikan' "Jikan must not be a saga source."
$saProviders = Read-File "qml/SagaApi.js"
Assert-Lacks $saProviders 'AniList' "SagaApi must not search AniList (manga identities are curated pins)."
Assert-Lacks $saProviders 'Jikan' "SagaApi must not search Jikan (manga identities are curated pins)."

# --- the saga engine: canon in, fuzz out ---
$sa = Read-File "qml/SagaApi.js"
Assert-Contains $sa 'function slotByCanon(' "SagaApi must slot hits by curated canon order."
Assert-Contains $sa '.import "BiblioApi.js" as Biblio' "Books must come from the Biblio lane."
Assert-Contains $sa 'Biblio.lookupBook' "Each novel must resolve to a real Biblio book."

# --- routing: saga category opens the saga page; books route to openBook ---
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'SagaUniversePage.qml' "Main must route saga universes to the saga template."
Assert-Contains $main 'item.bookRequested.connect(win.openBook)' "Saga books must open the Biblio detail."

Write-Host "saga universe p0: OK"

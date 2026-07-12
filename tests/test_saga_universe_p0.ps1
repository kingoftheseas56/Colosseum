$ErrorActionPreference = "Stop"

# Saga universe template contract (Hemanth correction 2026-07-12): book-first IPs get their
# OWN template — curated canon, Read = book one via Biblio, Watch = film one, no manga ever.
# The behavioral truth rides the canon harness (exit code); this runs it then greps shape.

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

$harness = Join-Path $PSScriptRoot "saga_canon_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "saga canon harness failed (exit $LASTEXITCODE)" }

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

# --- the saga page: books are the spine, manga does not exist here ---
$sp = Read-File "qml/SagaUniversePage.qml"
Assert-Contains $sp 'signal bookRequested(var book)' "Saga page must carry the book verb."
Assert-Contains $sp 'root.bookRequested(root.firstBook)' "Read must route to book ONE."
Assert-Contains $sp 'SagaApi.js' "Saga page must ride SagaApi (curated canon, not name-search)."
Assert-Lacks $sp 'manga' "The word manga must not exist on the saga template (the LOTR anthology lesson)."
Assert-Lacks $sp 'AniList' "AniList must not be a saga source."

# --- the saga engine: canon in, fuzz out ---
$sa = Read-File "qml/SagaApi.js"
Assert-Contains $sa 'function slotByCanon(' "SagaApi must slot hits by curated canon order."
Assert-Contains $sa '.import "BiblioApi.js" as Biblio' "Books must come from the Biblio lane."
Assert-Contains $sa 'Biblio.lookupBook' "Each novel must resolve to a real Biblio book."

# --- routing: saga category opens the saga page; books route to openBook ---
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'SagaUniversePage.qml' "Main must route saga universes to the saga template."
Assert-Contains $main 'item.bookRequested.connect(win.openBook)' "Saga books must open the Biblio detail."

# --- the generic template: honest labels, no default universe, manga suppressible ---
$up = Read-File "qml/UniversePage.qml"
Assert-Contains $up 'property string universeName: ""' "No default universe (the One Piece flash lesson)."
Assert-Contains $up 'root.seriesLabel' "Series row label must come from curation (TV Shows for western IPs)."
Assert-Contains $up 'root.hasRead' "readMode none must suppress the manga machinery."

Write-Host "saga universe p0: OK"

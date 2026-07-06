$ErrorActionPreference = "Stop"

# Back-navigation unification contract (spec: haven docs/superpowers/specs/
# 2026-07-05-colosseum-back-navigation-design.md). One BackAction component, one vector
# chevron; no font-glyph back arrows; player minimize is honestly labelled.

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

# --- the component itself ---
$ba = Read-File "qml/BackAction.qml"
Assert-Contains $ba 'import QtQuick.Shapes' "BackAction must draw a vector chevron (Shapes), never a font glyph."
Assert-Contains $ba 'ShapePath' "BackAction must own the canonical chevron ShapePath."
Assert-Contains $ba 'signal triggered()' "BackAction must expose triggered()."
foreach ($v in @('"plain"', '"capsule"', '"immersive"')) {
    Assert-Contains $ba $v "BackAction must support variant $v."
}

# --- the ASCII '<' glyph bug must never return, anywhere ---
Get-ChildItem (Join-Path $root "qml") -Filter *.qml | ForEach-Object {
    $t = Get-Content $_.FullName -Raw
    Assert-Lacks $t 'text: "<"' "$($_.Name): ASCII '<' back glyph is banned (TheatreSeries drift bug)."
}

# --- every migrated surface instantiates the shared component ---
$migrated = @(
    "qml/TheatreSeries.qml", "qml/MangaSeries.qml", "qml/ComicSeries.qml",
    "qml/ComicArchiveIndex.qml", "qml/SourcesSheet.qml", "qml/BiblioBook.qml",
    "qml/TopBar.qml", "qml/SearchSurface.qml", "qml/BiblioSearch.qml",
    "qml/GenrePage.qml", "qml/GenreIndex.qml", "qml/TheatreGenrePage.qml",
    "qml/TheatreGenreIndex.qml", "qml/BiblioGenrePage.qml", "qml/BiblioGenreIndex.qml",
    "qml/UniversePage.qml", "qml/CinematicPage.qml", "qml/ExtensionsPage.qml",
    "qml/MangaReader.qml"
)
foreach ($f in $migrated) {
    Assert-Contains (Read-File $f) 'BackAction {' "$f must use the shared BackAction component."
}

# --- hand-typed back-chevron Texts are gone from the migrated back controls ---
# (page-turn / shelf-scroll arrows and destination actions like '‹ Stay here' are allowed;
#  the banned pattern is the display-font back-button glyph next to a backRequested handler)
foreach ($f in @("qml/TheatreSeries.qml", "qml/MangaSeries.qml", "qml/ComicSeries.qml",
                 "qml/ComicArchiveIndex.qml", "qml/SourcesSheet.qml", "qml/TopBar.qml")) {
    $t = Read-File $f
    Assert-Lacks $t 'id: backRow' "$f still carries the hand-built back row."
}

# --- search surfaces expose a VISIBLE exit (Esc is a hint, not the only door) ---
foreach ($f in @("qml/SearchSurface.qml", "qml/BiblioSearch.qml")) {
    Assert-Contains (Read-File $f) 'id: searchBack' "$f must carry the visible header BackAction exit."
}

# --- player: the top-left control is an honest minimize, not a fake Back ---
$pp = Read-File "qml/PlayerPage.qml"
Assert-Contains $pp 'icon: "minimizeToBar"' "Player top-left control must wear the minimize icon."
Assert-Contains $pp 'tooltip: "Minimize' "Player top-left tooltip must say Minimize, not Back."
Assert-Contains $pp 'kind === "minimizeToBar"' "Player Canvas must paint the minimizeToBar glyph."
Assert-Contains $pp 'root.minimizeRequested()' "Player top-left control must emit minimizeRequested."

# --- EPUB reader shares the canonical chevron geometry ---
$html = Read-File "resources/book_reader/ebook_reader.html"
Assert-Contains $html 'M14.5 5.5 L8 12 L14.5 18.5' "EPUB toolbar must use the canonical chevron path."

Write-Host "Back-action unification contract checks passed."

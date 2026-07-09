$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) { Get-Content (Join-Path $root $rel) -Raw }
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

$historyPath = Join-Path $root "qml/SearchHistory.js"
if (!(Test-Path $historyPath)) {
    throw "Search history must live in a shared QML JS library so Loader-recreated search bars can show previous queries."
}

$history = Get-Content $historyPath -Raw
$surface = Read-File "qml/SearchSurface.qml"
$biblio = Read-File "qml/BiblioSearch.qml"

Assert-Contains $history ".pragma library" `
    "SearchHistory.js must use QML's library pragma so history survives component destruction."
Assert-Contains $history "function record(scope, query)" `
    "SearchHistory.js must expose a scoped record() helper."
Assert-Contains $history "function list(scope)" `
    "SearchHistory.js must expose a scoped list() helper for reopened search bars."
Assert-Contains $history "function remove(scope, query)" `
    "SearchHistory.js must expose scoped removal for recent chips."

Assert-Contains $surface 'import "SearchHistory.js" as SearchHistory' `
    "SearchSurface must import the shared search history library."
Assert-Matches $surface 'function\s+historyScope\(\)[\s\S]*searchMode' `
    "SearchSurface history must be scoped by world search mode."
Assert-Contains $surface "onSearchModeChanged: surf.loadRecent()" `
    "SearchSurface must reload persisted history after Main assigns Tankoban/Theatre mode."
Assert-Contains $surface "surf.recent = SearchHistory.record(surf.historyScope(), q)" `
    "SearchSurface must record successful searches into shared history."
Assert-Contains $surface "surf.recent = SearchHistory.remove(surf.historyScope(), q)" `
    "SearchSurface recent-chip removal must update shared history."

Assert-Contains $biblio 'import "SearchHistory.js" as SearchHistory' `
    "BiblioSearch must import the shared search history library."
Assert-Contains $biblio 'search.recent = SearchHistory.list("Biblio")' `
    "BiblioSearch must load persisted book history on creation."
Assert-Contains $biblio 'search.recent = SearchHistory.record("Biblio", q)' `
    "BiblioSearch must record successful searches into shared history."

Write-Host "Search history persistence contract checks passed."

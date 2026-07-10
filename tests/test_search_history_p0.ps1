$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) { Get-Content (Join-Path $root $rel) -Raw }
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

$surface = Read-File "qml/SearchSurface.qml"
$biblio = Read-File "qml/BiblioSearch.qml"
$main = Read-File "native/main.cpp"
$store = Read-File "native/SearchHistoryStore.h"

Assert-Contains $main 'setContextProperty(QStringLiteral("SearchHistory"), history)' `
    "main.cpp must expose the native SearchHistory service to QML."
Assert-Contains $store 'Q_INVOKABLE QStringList record' "Native store must expose record()."
Assert-Contains $store 'Q_INVOKABLE QStringList remove' "Native store must expose remove()."
Assert-Contains $store 'Q_INVOKABLE void clear' "Native store must expose clear()."
Assert-Contains $surface 'surf.historyStore = SearchHistory' "SearchSurface must bind the native history service."
Assert-Contains $surface 'surf.historyStore.list(surf.historyScope())' "SearchSurface must load native history."
Assert-Contains $surface 'function historyScope() { return surf.searchMode.toLowerCase() }' "SearchSurface must use stable lower-case scopes."
Assert-Contains $surface 'function commitCurrentQuery()' "SearchSurface must commit intentional dispatched queries separately."
Assert-Contains $surface 'surf.historyStore.record(surf.historyScope(), q)' "SearchSurface must use native history record."
Assert-NotContains $surface 'surf.recordRecent(q)' "Provider completion must not be the history commit path."
Assert-Contains $biblio 'search.historyStore = SearchHistory' "Biblio must bind the native history service."
Assert-Contains $biblio 'search.historyStore.list("biblio")' "Biblio must load its own native scope."
Assert-Contains $biblio 'function commitCurrentQuery()' "Biblio must commit intentional dispatched queries separately."
Assert-Contains $biblio 'search.historyStore.remove("biblio", q)' "Biblio must support native individual removal."
Assert-Matches $biblio 'id:\s*removeRecentMa[\s\S]*onClicked:\s*search\.removeRecent\(modelData\)' `
    "Biblio must have a distinct remove control that removes without running search."
Assert-Contains $biblio 'objectName: "biblioRecentBody"' "Biblio chip body must be independently testable."
Assert-Contains $biblio 'objectName: "biblioRecentRemove"' "Biblio remove target must be independently testable."

$harness = Join-Path $root "native\build-msvc\search_history_store_harness.exe"
if (!(Test-Path $harness)) { throw "Build search_history_store_harness before running this test." }
& $harness
if ($LASTEXITCODE -ne 0) { throw "Native behavioral harness failed." }

& 'C:\Qt\6.11.1\msvc2022_64\bin\qmltestrunner.exe' -input (Join-Path $root "tests\qml")
if ($LASTEXITCODE -ne 0) { throw "QML provider-independence and Loader-recreation harness failed." }

$probePath = Join-Path ([System.IO.Path]::GetTempPath()) ("colosseum-search-history-" + [guid]::NewGuid().ToString() + ".ini")
try {
    & $harness --write $probePath
    if ($LASTEXITCODE -ne 0) { throw "Restart writer process failed." }
    & $harness --verify $probePath
    if ($LASTEXITCODE -ne 0) { throw "Restart reader process failed." }
} finally {
    Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue
}

Write-Host "Search history native contract, behavior, and restart checks passed."

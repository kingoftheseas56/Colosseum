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
$world = Read-File "qml/WorldSearch.js"
$biblioApi = Read-File "qml/BiblioApi.js"
$runtime = Read-File "native/account/ProfileStoreRuntime.cpp"
$store = Read-File "native/SearchHistoryStore.h"
$center = Read-File "qml/account/AccountCenter.qml"

Assert-Contains $runtime 'setContextProperty(' "ProfileStoreRuntime must bind QML context properties."
Assert-Matches $runtime 'setContextProperty\(\s*QStringLiteral\("SearchHistory"\),\s*m_stores->searchHistory\.get\(\)\)' `
    "ProfileStoreRuntime must expose the active profile SearchHistory owner to QML."
Assert-Contains $store 'Q_PROPERTY(bool rememberEnabled' "SearchHistoryStore must expose remembering policy."
Assert-Contains $store 'setRememberEnabled' "SearchHistoryStore must let the privacy owner change remembering policy."
Assert-Matches $store 'record\([\s\S]*?if \(!m_rememberEnabled\)' "record() must refuse persistence while remembering is disabled."
Assert-Contains $surface 'property var historyStore: typeof SearchHistory !== "undefined" ? SearchHistory : null' `
    "SearchSurface must follow the current profile SearchHistory context owner."
Assert-Contains $surface 'onHistoryStoreChanged:' "SearchSurface must refresh recents when the profile history owner changes."
Assert-Contains $surface 'property string searchError:' "SearchSurface must expose provider failure separately from empty results."
Assert-Contains $surface 'objectName: searchMode.length ? searchMode.toLowerCase() + "SearchSurface" : "searchSurface"' `
    "SearchSurface must expose a world-qualified runtime identity."
Assert-Contains $surface 'readonly property int resultCount:' "SearchSurface must expose read-only result count for runtime proof."
Assert-Contains $surface 'readonly property int recentCount:' "SearchSurface must expose read-only recent count for runtime proof."
Assert-Contains $surface 'readonly property bool showingProviderError:' "SearchSurface must expose provider-error truth for runtime proof."
Assert-Contains $surface 'readonly property bool showingNoResults:' "SearchSurface must expose ordinary-empty truth for runtime proof."
Assert-NotContains $surface 'surf.historyStore = SearchHistory' "SearchSurface must not capture one profile owner imperatively."
Assert-Contains $biblio 'property var historyStore: typeof SearchHistory !== "undefined" ? SearchHistory : null' `
    "BiblioSearch must follow the current profile SearchHistory context owner."
Assert-Contains $biblio 'onHistoryStoreChanged:' "BiblioSearch must refresh recents when the profile history owner changes."
Assert-Contains $biblio 'property string searchError:' "BiblioSearch must expose provider failure separately from empty results."
Assert-Contains $biblio 'objectName: "biblioSearchSurface"' "BiblioSearch must expose a stable runtime identity."
Assert-Contains $biblio 'objectName: "biblioSearchInput"' "BiblioSearch input must expose a stable runtime identity."
Assert-Contains $biblio 'readonly property int bookResultCount:' "BiblioSearch must expose read-only book result count."
Assert-Contains $biblio 'readonly property int audioResultCount:' "BiblioSearch must expose read-only audiobook result count."
Assert-Contains $biblio 'readonly property int recentCount:' "BiblioSearch must expose read-only recent count."
Assert-Contains $biblio 'readonly property bool showingProviderError:' "BiblioSearch must expose provider-error truth."
Assert-Contains $biblio 'readonly property bool showingNoResults:' "BiblioSearch must expose ordinary-empty truth."
Assert-Contains $biblio 'property var audioSearchDispatcher:' "Biblio audiobook search must be injectable and failure-testable."
Assert-NotContains $biblio 'search.historyStore = SearchHistory' "BiblioSearch must not capture one profile owner imperatively."

Assert-Contains $center 'onPrivacyRememberSearchHistoryChangeRequested:' `
    "AccountCenter must give the Remember search history switch an authoritative handler."
Assert-Matches $center 'privacyRememberSearchHistory:\s*searchHistoryStore\s*\?\s*searchHistoryStore\.rememberEnabled' `
    "AccountCenter must project the store's authoritative remembering policy."
Assert-Matches $world 'done\(null,\s*[^\)]*error' "WorldSearch request failures must carry failure state."
Assert-Contains $world 'provider unavailable' "WorldSearch must have an explicit provider-unavailable failure contract."
Assert-Matches $world 'done\(all2,\s*failures\s*>\s*0\s*\?\s*PROVIDER_UNAVAILABLE\s*:\s*""\)' "Theatre hero-enrichment pass must carry provider failure state (F0010-6B)."
Assert-Matches $biblioApi 'done\(null,\s*[^\)]*error' "BiblioApi request failures must carry failure state."

Write-Host "Function 0010 search history, privacy, rebinding, and failure-truth source contract passed."

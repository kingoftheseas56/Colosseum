$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$worldSearch = Get-Content (Join-Path $root 'qml/WorldSearch.js') -Raw
$biblioApi = Get-Content (Join-Path $root 'qml/BiblioApi.js') -Raw
$search = Get-Content (Join-Path $root 'qml/SearchSurface.qml') -Raw
$biblio = Get-Content (Join-Path $root 'qml/BiblioSearch.qml') -Raw
$historyTest = Get-Content (Join-Path $root 'tests/qml/tst_search_history_flow.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($worldSearch.Contains('xhr.abort()') -and $worldSearch.Contains('xhr.timeout = REQUEST_TIMEOUT_MS')) `
    'WorldSearch transport must expose abort and a bounded timeout.'
Need ($worldSearch.Contains('return cancel;')) `
    'WorldSearch multi-request operations must return aggregate cancellation handles.'
Need ($biblioApi.Contains('xhr.abort()') -and $biblioApi.Contains('xhr.timeout = REQUEST_TIMEOUT_MS')) `
    'BiblioApi transport must expose abort and a bounded timeout.'
Need ($search.Contains('function cancelAllRequests()') -and $search.Contains('surf.cancelAllRequests()')) `
    'SearchSurface must cancel pending work on lifecycle exits.'
Need ($search.Contains('debounce.stop(); surf.runSearch()')) `
    'SearchSurface Enter submission must stop the pending debounce before dispatching immediately.'
Need ($biblio.Contains('function cancelAllRequests()') -and $biblio.Contains('search.audioSearchDispatcher')) `
    'BiblioSearch must cancel both book and audiobook lanes.'
Need ($biblio.Contains('debounce.stop(); search.runAppleSearch()')) `
    'BiblioSearch Enter submission must stop the pending debounce before dispatching immediately.'
Need ($historyTest.Contains('test_genericSearchCancelsSupersededRequestAndRejectsLateResult')) `
    'Search history tests must cover superseded requests and late callbacks.'
Need ($historyTest.Contains('test_searchDestructionCancelsBookAndAudioRequests')) `
    'Search history tests must cover Loader destruction cancellation.'

Write-Host 'Search cancellation contract: PASS'

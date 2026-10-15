$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$addon = Get-Content (Join-Path $root 'qml/AddonClient.js') -Raw
$discoverApi = Get-Content (Join-Path $root 'qml/DiscoverApi.js') -Raw
$discoverPage = Get-Content (Join-Path $root 'qml/DiscoverPage.qml') -Raw
$browser = Get-Content (Join-Path $root 'qml/DiscoverBrowser.qml') -Raw
$theatreApi = Get-Content (Join-Path $root 'qml/TheatreApi.js') -Raw
$theatrePage = Get-Content (Join-Path $root 'qml/TheatreCatalogPage.qml') -Raw
$biblioApi = Get-Content (Join-Path $root 'qml/BiblioDiscoverApi.js') -Raw
$biblioExplore = Get-Content (Join-Path $root 'qml/BiblioExplorePage.qml') -Raw
$browserHarness = Get-Content (Join-Path $root 'tests/discover_browser_harness.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($addon.Contains('xhr.abort()') -and $addon.Contains('xhr.timeout = timeoutMs')) `
    'Addon transport must expose abort and a bounded timeout.'
Need ($addon.Contains('return _get(url, FAST_TIMEOUT_MS')) `
    'Addon catalog fetches must return their transport cancellation handle.'
Need ($discoverApi.Contains('return AddonClient.fetchCatalogUrl')) `
    'DiscoverApi must propagate the addon cancellation handle.'
Need ($discoverPage.Contains('return Api.loadPage')) `
    'Theatre Discover adapter must propagate the page cancellation handle.'
Need ($browser.Contains('function cancelPageRequest()') -and $browser.Contains('browser.cancelPageRequest()') -and
      $browser.Contains('_pageResumePending = loading && !exhausted') -and
      $browser.Contains('pageResumePending: _pageResumePending')) `
    'DiscoverBrowser must cancel its current page request on lifecycle changes.'
Need ($browserHarness.Contains('retained-wall cancellation regression') -and
      $browserHarness.Contains('pageCancelFake.cancelCount === 1') -and
      $browserHarness.Contains('resumes its canceled next page')) `
    'DiscoverBrowser harness must cover cancel-and-resume of a deferred next page.'
Need ($theatreApi.Contains('var injected = requestAdapter(url, done)') -and $theatreApi.Contains('return xhrCancel')) `
    'TheatreApi request seams must preserve cancellation handles.'
Need ($theatrePage.Contains('function cancelLoad()') -and $theatrePage.Contains('page.cancelLoad()')) `
    'Theatre catalogue pages must cancel hidden and superseded loads.'
Need ($biblioApi.Contains('return DiscoverApi.loadPage')) `
    'Biblio Discover adapters must propagate extension page cancellation.'
Need ($biblioExplore.Contains('function cancelExtensionRequests()') -and
      $biblioExplore.Contains('page.cancelExtensionRequests()')) `
    'Biblio Explore must cancel hidden extension requests.'

Write-Host 'Catalogue cancellation contract: PASS'

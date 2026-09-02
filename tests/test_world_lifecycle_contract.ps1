$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$worldPage = Get-Content (Join-Path $root 'qml/WorldPage.qml') -Raw
$topBar = Get-Content (Join-Path $root 'qml/TopBar.qml') -Raw
$browser = Get-Content (Join-Path $root 'qml/DiscoverBrowser.qml') -Raw
$discover = Get-Content (Join-Path $root 'qml/DiscoverPage.qml') -Raw
$biblioDiscover = Get-Content (Join-Path $root 'qml/BiblioDiscoverPage.qml') -Raw
$tankobanDiscover = Get-Content (Join-Path $root 'qml/TankobanDiscoverPage.qml') -Raw
$theatreCatalog = Get-Content (Join-Path $root 'qml/TheatreCatalogPage.qml') -Raw
$biblioExplore = Get-Content (Join-Path $root 'qml/BiblioExplorePage.qml') -Raw
$biblio = Get-Content (Join-Path $root 'qml/BiblioWorld.qml') -Raw
$theatre = Get-Content (Join-Path $root 'qml/TheatreWorld.qml') -Raw
$tankoban = Get-Content (Join-Path $root 'qml/TankobanWorld.qml') -Raw
$theatreLibrary = Get-Content (Join-Path $root 'qml/LibraryPage.qml') -Raw
$biblioLibrary = Get-Content (Join-Path $root 'qml/BiblioLibraryPage.qml') -Raw
$tankobanLibrary = Get-Content (Join-Path $root 'qml/TankobanLibraryTab.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($worldPage.Contains('property bool lifecycleActive: true')) `
    'WorldPage must expose the retained-world lifecycle seam.'
Need ($topBar.Contains('running: bar.lifecycleActive')) `
    'Retained hidden worlds must stop their TopBar clock timer.'
Need ($main.Contains('visible: worldStack.current === mode && !win.immersiveSurfaceOpen')) `
    'Retained worlds must stop painting underneath immersive surfaces.'
Need ($main.Contains('"lifecycleActive": worldStack.current === mode && !win.immersiveSurfaceOpen')) `
    'Main must pass an immersive-aware initial world activation state before world completion.'
Need ($main.Contains('return worldStack.current === mode && !win.immersiveSurfaceOpen')) `
    'Main must suspend retained-world lifecycle work underneath immersive surfaces.'
Need ($main.Contains('BiblioCatalog.setBackgroundWorkSuspended(win.immersiveSurfaceOpen)')) `
    'Immersive surfaces must suspend global Biblio network work, not only hide the Biblio world.'
Need ($browser.Contains('property bool active: true')) `
    'DiscoverBrowser must expose an activation gate.'
Need ($browser.Contains('if (!active || !adapter || loading')) `
    'Hidden DiscoverBrowser instances must not start page requests.'
Need ($discover.Contains('active: disco.active')) `
    'The Theatre Discover wrapper must forward world activation.'
Need ($biblioDiscover.Contains('active: root.active')) `
    'The Biblio Discover wrapper must forward world activation.'
Need ($tankobanDiscover.Contains('property bool active: true') -and $tankobanDiscover.Contains('active: root.active')) `
    'The Tankoban Discover wrapper must forward world activation.'
Need ($biblio.Contains('if (!biblio.lifecycleActive)')) `
    'Biblio native featured refresh must stand down while hidden.'
Need ($theatre.Contains('function activateContent()') -and $theatre.Contains('onLifecycleActiveChanged')) `
    'Theatre warmers must be deferred behind a lifecycle activation function.'
Need ($tankoban.Contains('function initializeComicCatalogue()') -and $tankoban.Contains('onLifecycleActiveChanged')) `
    'Tankoban catalogue derivation must be deferred behind a lifecycle activation function.'
Need ($theatreCatalog.Contains('property bool active: true') -and $theatreCatalog.Contains('if (!page.active) return')) `
    'Hidden Theatre catalogue pages must not start a catalog load.'
Need ($biblioExplore.Contains('property bool active: true') -and $biblioExplore.Contains('if (!page.active) return')) `
    'Hidden Biblio Explore pages must not start extension fetches.'
Need ($theatre.Contains('active: theatre.lifecycleActive && visible')) `
    'Theatre must bind catalogue-page activity to world and tab visibility.'
Need ($biblio.Contains('active: biblio.lifecycleActive && visible')) `
    'Biblio must bind Explore activity to world and tab visibility.'
Need ($theatre.Contains('active: theatre.lifecycleActive && visible')) `
    'Theatre must bind Discover activity to world and tab visibility.'
Need ($tankoban.Contains('active: tanko.lifecycleActive && visible')) `
    'Tankoban must bind Discover activity to world and tab visibility.'
Need ($tankoban.Contains('items: tanko.lifecycleActive ? (Progress.revision, tanko.nextUpRows()) : []')) `
    'Hidden Tankoban worlds must not query Progress/Downloads for Next Up.'
Need ($tankoban.Contains('items: tanko.lifecycleActive ? (Progress.revision, (function()')) `
    'Hidden Tankoban worlds must not query Progress for Continue Reading.'
Need ($theatre.Contains('property var continueRows: theatre.lifecycleActive')) `
    'Hidden Theatre worlds must not query Progress for Continue Watching.'
Need ($biblio.Contains('items: biblio.lifecycleActive')) `
    'Hidden Biblio worlds must not query Progress for Continue Reading.'
Need ($theatre.Contains('active: theatre.lifecycleActive && visible')) `
    'Theatre must gate its retained Library model behind lifecycle and tab visibility.'
Need ($biblio.Contains('active: biblio.lifecycleActive && visible')) `
    'Biblio must gate its retained Library model behind lifecycle and tab visibility.'
Need ($tankoban.Contains('active: tanko.lifecycleActive && visible')) `
    'Tankoban must gate its retained Library model behind lifecycle and tab visibility.'
Need ($theatreLibrary.Contains('property bool active: true') -and $theatreLibrary.Contains('if (!isActive')) `
    'Theatre LibraryPage must expose an activation gate before deriving its model.'
Need ($biblioLibrary.Contains('property bool active: true') -and $biblioLibrary.Contains('if (!isActive')) `
    'Biblio LibraryPage must expose an activation gate before deriving its model.'
Need ($tankobanLibrary.Contains('property bool active: true') -and $tankobanLibrary.Contains('if (!isActive')) `
    'Tankoban LibraryPage must expose an activation gate before deriving its model.'

# Vault is a large taskbar-only surface. Its Loader must stage construction asynchronously;
# openVaultPage() only flips active and vaultBack() already handles item-not-yet-loaded.
$vaultLoaderStart = $main.IndexOf('id: vaultLayer')
$vaultLoaderEnd = $main.IndexOf('source: "VaultPage.qml"', $vaultLoaderStart)
Need ($vaultLoaderStart -ge 0 -and $vaultLoaderEnd -gt $vaultLoaderStart `
      -and $main.Substring($vaultLoaderStart, $vaultLoaderEnd - $vaultLoaderStart).Contains('visible: active && !win.immersiveSurfaceOpen')) `
    'The retained Vault page must stop painting underneath immersive surfaces.'
Need ($vaultLoaderStart -ge 0 -and $vaultLoaderEnd -gt $vaultLoaderStart `
      -and $main.Substring($vaultLoaderStart, $vaultLoaderEnd - $vaultLoaderStart).Contains('asynchronous: true')) `
    'The large VaultPage Loader must construct asynchronously to preserve shell responsiveness.'

# Downloads has the same taskbar-only lifecycle and is also loaded only on demand. Verify its
# Loader remains frame-friendly; item-dependent route wiring lives exclusively in onLoaded.
$downloadsLoaderStart = $main.IndexOf('id: downloadsLayer')
$downloadsLoaderEnd = $main.IndexOf('source: "DownloadsPage.qml"', $downloadsLoaderStart)
Need ($downloadsLoaderStart -ge 0 -and $downloadsLoaderEnd -gt $downloadsLoaderStart `
      -and $main.Substring($downloadsLoaderStart, $downloadsLoaderEnd - $downloadsLoaderStart).Contains('asynchronous: true')) `
    'The large DownloadsPage Loader must construct asynchronously to preserve shell responsiveness.'

Write-Host 'World lifecycle contract: PASS'

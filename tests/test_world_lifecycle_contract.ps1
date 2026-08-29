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

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($worldPage.Contains('property bool lifecycleActive: true')) `
    'WorldPage must expose the retained-world lifecycle seam.'
Need ($topBar.Contains('running: bar.lifecycleActive')) `
    'Retained hidden worlds must stop their TopBar clock timer.'
Need ($main.Contains('"lifecycleActive": worldStack.current === mode')) `
    'Main must pass the initial world activation state before world completion.'
Need ($main.Contains('return worldStack.current === mode')) `
    'Main must keep the world lifecycle seam bound to the current world.'
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

Write-Host 'World lifecycle contract: PASS'

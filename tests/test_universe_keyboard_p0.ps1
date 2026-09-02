$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Read-Rel([string]$rel) {
    $path = Join-Path $root $rel
    if (-not (Test-Path $path)) { throw "missing $rel" }
    return Get-Content -Raw $path
}
function Need([string]$rel, [string]$needle, [string]$why) {
    $text = Read-Rel $rel
    if (-not $text.Contains($needle)) { throw "${rel}: $why" }
}
function NeedCount([string]$rel, [string]$needle, [int]$atLeast, [string]$why) {
    $text = Read-Rel $rel
    $count = ([regex]::Matches($text, [regex]::Escape($needle))).Count
    if ($count -lt $atLeast) { throw "${rel}: $why (found $count, need >= $atLeast)" }
}

# Shared K-04 adapters: semantic chrome, one-Tab rails, and true column-major spatial collections.
Need 'qml\UniverseChromeAction.qml' 'KeyboardAction {' 'system/search chrome must expose keyboard activation.'
Need 'qml\UniverseRailFocus.qml' 'KeyboardCollectionController {' 'horizontal rails must be composite regions.'
Need 'qml\UniverseRailFocus.qml' 'orientation: "horizontal"' 'rail arrows must follow the horizontal visual axis.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_Left' 'spatial collections need horizontal movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_Up' 'spatial collections need vertical movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_PageDown' 'spatial collections need page movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'root.activated(' 'spatial activation must reuse the page semantic action.'

# Generic universe and data-driven extension.
Need 'qml\UniversePage.qml' 'id: halfKey' 'Read/Watch duality must be keyboard reachable.'
Need 'qml\UniversePage.qml' 'UniverseRailFocus {' 'each medium row must be a single composite Tab stop.'
Need 'qml\UniversePage.qml' 'accessibleName: "Search"' 'search icon needs a semantic keyboard twin.'
Need 'qml\UniversePage.qml' 'Qt.Key_Escape' 'Esc must route to Back.'
Need 'qml\UniverseTile.qml' 'focusManagedByCollection' 'tiles must support collection-owned focus without duplicate Tab stops.'
Need 'qml\UniverseTile.qml' 'KeyboardAction {' 'standalone tile activation must have a keyboard path.'
Need 'qml\UniverseExtensionPage.qml' 'focusManagedByCollection: true' 'extension tiles must not each become Tab stops.'
Need 'qml\UniverseExtensionPage.qml' 'KeyboardCollectionController {' 'extension rails need local arrows and activation.'
Need 'qml\UniverseExtensionPage.qml' 'sectionRail.currentIndex' 'extension pointer and keyboard selection must share one current item.'

# Hall ledger: one vertical collection, keyboard selection produces the same breathing state as hover.
Need 'qml\UniverseHallPage.qml' 'focusPolicy: root.universes.length > 0 ? Qt.TabFocus : Qt.NoFocus' 'hall ledger must be one focus region.'
Need 'qml\UniverseHallPage.qml' 'orientation: "vertical"' 'hall arrows must follow its vertical pile.'
Need 'qml\UniverseHallPage.qml' 'stackWalk.activeFocus && stackWalk.currentIndex === bar.index' 'keyboard selection must breathe the active ledger row.'
Need 'qml\UniverseHallPage.qml' 'hallNav.keyboardRecentlyMoved' 'stationary hover must not overwrite keyboard selection.'
Need 'qml\UniverseHallPage.qml' 'Qt.Key_Escape' 'Esc must leave the hall.'

# Era page: ragged columns + comics singleton; book/extras rails stay composite.
Need 'qml\EraUniversePage.qml' 'id: eraGalleryFocus' 'era gallery needs spatial focus.'
Need 'qml\EraUniversePage.qml' 'itemsProperty: "items"' 'era spatial controller must walk each epoch column.'
Need 'qml\EraUniversePage.qml' 'appendSingleton: !!root.uni.comics' 'comics door must participate as the final spatial column.'
NeedCount 'qml\EraUniversePage.qml' 'UniverseRailFocus {' 2 'books and extra rails must each be composite regions.'
Need 'qml\EraUniversePage.qml' 'id: beginKey' 'golden-path watch button needs keyboard activation.'

# Star Wars evolved after the Arc 41 snapshot into StarWarsGalaxySystem + StarWarsMediaShelf.
# Verify the current runtime rather than the retired generic triptych identifiers.
Need 'qml\GalaxyUniversePage.qml' 'KeyboardScrollController {' 'destination view needs keyboard scrolling.'
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onUpPressed' 'galaxy destinations need Up navigation.'
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onDownPressed' 'galaxy destinations need Down navigation.'
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onSpacePressed' 'galaxy destinations need Space activation.'
Need 'qml\StarWarsMediaShelf.qml' 'Keys.onLeftPressed' 'media shelves need local Left navigation.'
Need 'qml\StarWarsMediaShelf.qml' 'Keys.onRightPressed' 'media shelves need local Right navigation.'

# Sagas: Read/Watch halves, ordered novel shelf, adaptation rails, comics door.
Need 'qml\SagaUniversePage.qml' 'id: halfKey' 'Read/Watch halves need keyboard activation.'
Need 'qml\SagaUniversePage.qml' 'id: bookNav' 'novel shelf must be a horizontal composite.'
Need 'qml\SagaUniversePage.qml' 'id: sagaComicsKey' 'saga comics archive door needs keyboard activation.'
Need 'qml\SagaUniversePage.qml' 'UniverseRailFocus {' 'adaptation shelves need local arrows.'

# Studio: the filmography is a responsive grid composite.
Need 'qml\StudioUniversePage.qml' 'readonly property int keyboardColumns' 'filmography grid columns must derive from visual width.'
Need 'qml\StudioUniversePage.qml' 'orientation: "grid"' 'filmography arrows must follow its wrapped grid.'
Need 'qml\StudioUniversePage.qml' 'id: beginKey' 'studio golden path needs keyboard activation.'

# Parked LOCG publisher page still closes its owner-brief actions.
Need 'qml\LocgPublisherPage.qml' 'id: sortKey' 'sort pills need keyboard activation.'
Need 'qml\LocgPublisherPage.qml' 'KeyNavigation.left' 'small stable sort row needs local left/right movement.'
Need 'qml\LocgPublisherPage.qml' 'orientation: "grid"' 'publisher poster wall needs true grid movement.'
Need 'qml\LocgPublisherPage.qml' 'grid.currentIndex = tile.index' 'pointer selection must synchronize the keyboard current item.'

# Every page-level system row is now implemented through the semantic chrome wrapper, and
# every page claims entry focus so Esc/PageUp/PageDown work before the first Tab.
foreach ($rel in @(
    'qml\EraUniversePage.qml','qml\GalaxyUniversePage.qml','qml\SagaUniversePage.qml',
    'qml\StudioUniversePage.qml','qml\UniverseExtensionPage.qml','qml\UniverseHallPage.qml',
    'qml\UniversePage.qml','qml\LocgPublisherPage.qml'
)) {
    Need $rel 'UniverseChromeAction {' 'minimize/fullscreen/close chrome must be keyboard reachable.'
    Need $rel 'forceActiveFocus(Qt.TabFocusReason)' 'page entry must establish deterministic keyboard focus.'
}

Write-Host 'UNIVERSE_KEYBOARD_P0_OK'

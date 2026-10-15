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
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_Left' 'spatial collections need horizontal movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_Up' 'spatial collections need vertical movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'Qt.Key_PageDown' 'spatial collections need page movement.'
Need 'qml\UniverseSpatialColumnsFocus.qml' 'root.activated(' 'spatial activation must reuse the page semantic action.'

# Generic universe and data-driven extension.
Need 'qml\UniverseTile.qml' 'focusManagedByCollection' 'tiles must support collection-owned focus without duplicate Tab stops.'
Need 'qml\UniverseTile.qml' 'KeyboardAction {' 'standalone tile activation must have a keyboard path.'
Need 'qml\UniverseExtensionPage.qml' 'focusManagedByCollection: true' 'extension tiles must not each become Tab stops.'
Need 'qml\UniverseExtensionPage.qml' 'KeyboardCollectionController {' 'extension rails need local arrows and activation.'
Need 'qml\UniverseExtensionPage.qml' 'sectionRail.currentIndex' 'extension pointer and keyboard selection must share one current item.'



# Star Wars evolved after the Arc 41 snapshot into StarWarsGalaxySystem + StarWarsMediaShelf.
# Verify the current runtime rather than the retired generic triptych identifiers.
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onUpPressed' 'galaxy destinations need Up navigation.'
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onDownPressed' 'galaxy destinations need Down navigation.'
Need 'qml\StarWarsGalaxySystem.qml' 'Keys.onSpacePressed' 'galaxy destinations need Space activation.'
Need 'qml\StarWarsMediaShelf.qml' 'Keys.onLeftPressed' 'media shelves need local Left navigation.'
Need 'qml\StarWarsMediaShelf.qml' 'Keys.onRightPressed' 'media shelves need local Right navigation.'

# Sagas (Arc 58 grid): one keyboard stop for the story grid, one per branch shelf, comics door.

# Shelves centre (Arc 58, 2026-10-04): the anime universes divide by medium; each medium
# row is ONE composite Tab stop, arrows follow the row, and the works walk across runs.

# Studio: the filmography is a responsive grid composite.

# Parked LOCG publisher page still closes its owner-brief actions.
Need 'qml\LocgPublisherPage.qml' 'id: sortKey' 'sort pills need keyboard activation.'
Need 'qml\LocgPublisherPage.qml' 'KeyNavigation.left' 'small stable sort row needs local left/right movement.'
Need 'qml\LocgPublisherPage.qml' 'orientation: "grid"' 'publisher poster wall needs true grid movement.'
Need 'qml\LocgPublisherPage.qml' 'grid.currentIndex = tile.index' 'pointer selection must synchronize the keyboard current item.'

# One Piece was rewritten after Arc 41. Keep its bespoke atlas/catalogue pages inside the same
# keyboard law: deterministic page focus plus semantic minimize/fullscreen/close chrome.
foreach ($rel in @(
    'qml\OnePieceUniversePage.qml','qml\OnePieceArcPage.qml'
)) {
    NeedCount $rel 'UniverseChromeAction {' 3 'One Piece system chrome must be keyboard reachable.'
    Need $rel 'focus: true' 'One Piece page entry must establish a keyboard origin.'
    Need $rel 'activeFocusOnTab: true' 'One Piece page origin must participate in Tab traversal.'
}

# Every page-level system row is now implemented through the semantic chrome wrapper, and
# every page claims entry focus so Esc/PageUp/PageDown work before the first Tab.
foreach ($rel in @(
    'qml\GalaxyUniversePage.qml','qml\SagaUniversePage.qml',
    'qml\ShelvesUniversePage.qml','qml\UniverseExtensionPage.qml',
    'qml\LocgPublisherPage.qml'
)) {
    Need $rel 'UniverseChromeAction {' 'minimize/fullscreen/close chrome must be keyboard reachable.'
    Need $rel 'forceActiveFocus(Qt.TabFocusReason)' 'page entry must establish deterministic keyboard focus.'
}

Write-Host 'UNIVERSE_KEYBOARD_P0_OK'

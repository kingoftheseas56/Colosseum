param([string]$RootOverride = "")
$ErrorActionPreference = "Stop"

$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$mainPath = Join-Path $root "qml\Main.qml"
if (-not (Test-Path $mainPath)) { throw "MISSING FILE: qml/Main.qml" }
$main = Get-Content $mainPath -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main.Contains('readonly property bool phoneWorldNavigationVisible:')) `
    'Main.qml must expose one phone-navigation visibility authority.'
Need ($main.Contains('win.androidHost && win.width < 840 && !win.immersiveSurfaceOpen')) `
    'Phone navigation must be narrow-Android only and suppress itself on immersive surfaces.'
Need ($main.Contains('visible: win.phoneWorldNavigationVisible')) `
    'PhoneWorldTabBar must bind visibility to the phone-navigation authority.'
Need ($main.Contains('return phoneWorldTabs.visible ? phoneWorldTabs.height : 0')) `
    'Hosted-page bottom inset must continue to follow the actual phone bar visibility.'

# S1.6 represents the three consumption classes. Their existing shell authority must remain intact.
Need ($main.Contains('readonly property bool immersiveSurfaceOpen: win.playerOpen')) `
    'Video player must remain part of immersiveSurfaceOpen.'
Need ($main.Contains('|| bookReaderLayer.active')) `
    'Book reader must remain part of immersiveSurfaceOpen.'
Need ($main.Contains('|| vaultComicLayer.active')) `
    'Standalone comic reader must remain part of immersiveSurfaceOpen.'
Need ($main.Contains('|| seriesLayer.active')) `
    'Tankoban reading room / manga reader path must remain part of immersiveSurfaceOpen.'
Need ($main.Contains('westernLayer.item.openChapterId.length > 0')) `
    'Western-comic reader path must remain part of immersiveSurfaceOpen.'
Need ($main.Contains('comicSeriesLayer.item.openChapterId.length > 0')) `
    'LOCG comic reader path must remain part of immersiveSurfaceOpen.'

$cmake = Get-Content (Join-Path $root "tests\CMakeLists.txt") -Raw
Need ($cmake.Contains('add_test(NAME colosseum.phone_immersive_nav_contract')) `
    'CTest must register the phone immersive-navigation contract.'
Need ($cmake.Contains('test_phone_immersive_nav.ps1')) `
    'CTest registration must invoke this exact contract script.'

Write-Host "phone immersive nav contract: PASS"

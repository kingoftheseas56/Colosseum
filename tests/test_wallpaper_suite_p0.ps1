# Wallpaper suite P0 (Axis 1+2, 2026-07-18): behavioral harness for the pure
# logic + grep contracts for the picker wiring.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# 1. the derivations behave (exit code IS the verdict).
$env:QT_QPA_PLATFORM = 'offscreen'
& $qmlExe (Join-Path $root 'tests/wallpaper_api_logic_harness.qml') | Out-Null
if ($LASTEXITCODE -ne 0) { throw "wallpaper_api_logic_harness FAILED (exit $LASTEXITCODE)" }

# 2. the picker pages instead of one-shotting, and carries the sort pills.
$ui = Get-Content (Join-Path $root 'qml/WallpaperSearch.qml') -Raw
if ($ui -notmatch 'fetchMore\(\)') { throw 'WallpaperSearch lost its Load more path' }
if ($ui -notmatch '"Load more"') { throw 'WallpaperSearch lost the Load more button' }
if ($ui -notmatch '"Random"') { throw 'WallpaperSearch lost the sort pills' }
if ($ui -notmatch 'freshState') { throw 'WallpaperSearch must build a paged search state' }

# 3. the API stays keyless and Wallhaven-only with the SFW gate pinned.
$api = Get-Content (Join-Path $root 'qml/WallpaperApi.js') -Raw
if ($api -notmatch 'purity=100') { throw 'Wallhaven SFW purity gate lost' }
if ($api -match 'api_key|apikey|token=') { throw 'a keyed source crept into the keyless wallpaper lane' }
# Konachan was removed 2026-07-20 (cheap-looking art) - assert no live wiring crept back
# (the historical note in the header keeps the word; the endpoint/functions must not return).
if ($api -match 'konachan\.net|mapKonachan|searchKonachan|konachanTags') { throw 'Konachan wiring crept back into the wallpaper lane - Wallhaven is the only source' }

# 4. native living wallpapers (the arena, 2026-07-18): scene exists, freezes, and routes.
$arena = Get-Content (Join-Path $root 'qml/wallpapers/ArenaNight.qml') -Raw
if ($arena -notmatch 'property bool running') { throw 'ArenaNight lost its running gate - it could never freeze' }
if ($arena -match 'Canvas\s*\{') { throw 'ArenaNight must stay scene-graph work, never per-frame Canvas painting' }
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
if ($main -notmatch 'wallpaperIsNative') { throw 'Main lost the native-wallpaper route' }
if ($main -notmatch 'nativeWallpaperFile') { throw 'Main lost the native-wallpaper registry' }
if ($main -notmatch 'immersiveSurfaceOpen[\s\S]{0,80}win\.visibility !== Window\.Minimized') {
    throw 'the native wallpaper must freeze on immersive surfaces and minimize (motion doctrine)'
}
if ($api -notmatch 'native:arena-night') { throw 'WallpaperApi lost the arena from nativePicks' }
if ($ui -notmatch '"Colosseum"') { throw 'the picker lost its Colosseum shelf' }

# 4a2. the app's own boot backdrop is a pickable tile (2026-07-20): the abstract
# render it ships with, offered back in the Colosseum shelf as a static image.
if ($api -notmatch 'function houseDefaultPick') { throw 'WallpaperApi lost the house default pick (the boot wallpaper)' }
if ($api -notmatch 'captured-motion') { throw 'house default must point at the bundled boot wallpaper' }
if (!(Test-Path (Join-Path $root 'assets/wallpaper/captured-motion.jpg'))) { throw 'the bundled boot wallpaper is missing' }
if ($ui -notmatch 'houseDefaultPick\(\)') { throw 'the Colosseum shelf must lead with the house default' }

# 4c. Captured Motion shelf (2026-07-20): Windows 11's Captured Motion theme, the
# dark abstract set the app boots to. The three remote siblings ride jsDelivr
# through the keyless wsrv.nl proxy (no repo bloat, no api key). The earlier
# literal Windows/macOS/Linux defaults were pulled - none fit - so guard they stay out.
if ($api -notmatch 'function capturedMotionPicks') { throw 'WallpaperApi lost the Captured Motion shelf' }
if ($api -notmatch 'wsrv\.nl') { throw 'Captured Motion picks must proxy through wsrv.nl' }
if ($api -notmatch 'cdn\.jsdelivr\.net') { throw 'Captured Motion picks must originate from the jsDelivr CDN' }
if ($ui -notmatch '"Captured Motion"') { throw 'the picker lost its Captured Motion shelf' }
if ($ui -notmatch 'WallpaperApi\.capturedMotionPicks\(\)') { throw 'the shelf must render capturedMotionPicks()' }
if ($api -match 'function osPicks') { throw 'the old OS-defaults shelf (osPicks) must be gone' }
foreach ($gone in @('macOS-Wallpapers', 'Distro-wallpapers', 'Bloom')) {
    if ($api -match [regex]::Escape($gone)) { throw "a pulled OS-default source ($gone) crept back - Hemanth wanted only Captured Motion" }
}

# 4b. Gilded Rain (2026-07-19): second native living wallpaper, same gates as the arena.
$rain = Get-Content (Join-Path $root 'qml/wallpapers/GildedRain.qml') -Raw
if ($rain -notmatch 'property bool running') { throw 'GildedRain lost its running gate - it could never freeze' }
if ($rain -match 'Canvas\s*\{') { throw 'GildedRain must stay scene-graph work, never per-frame Canvas painting' }
if ($api -notmatch 'native:gilded-rain') { throw 'WallpaperApi lost gilded-rain from nativePicks' }
# every native route must resolve to a scene through the one shared map (no more hardcoding).
if ($api -notmatch 'function nativeSceneFor') { throw 'WallpaperApi lost the single nativeSceneFor scene map' }
if ($api -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'nativeSceneFor does not route gilded-rain to its scene' }
if ($main -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'Main runtime map does not route gilded-rain to its scene' }

# 4d. Captured Motion, drawn by us (2026-07-20): still QML wallpapers (Qt Quick
# Shapes, no bitmap/shader/animation) in the Windows 11 Captured Motion spirit.
$ribbon = Get-Content (Join-Path $root 'qml/wallpapers/RibbonMotion.qml') -Raw
if ($ribbon -notmatch 'property int variant') { throw 'RibbonMotion lost its variant selector' }
if ($ribbon -notmatch 'property bool running') { throw 'RibbonMotion lost its running gate (shelf-contract parity)' }
if ($ribbon -match 'Canvas\s*\{') { throw 'RibbonMotion must stay scene-graph work, never per-frame Canvas painting' }
if ($ribbon -notmatch 'import QtQuick.Shapes') { throw 'RibbonMotion must be built from Qt Quick Shapes' }
foreach ($rv in @('ribbon-sunset', 'ribbon-dusk', 'ribbon-ember')) {
    if ($api -notmatch [regex]::Escape("native:$rv")) { throw "nativePicks lost the $rv wallpaper" }
    if ($api -notmatch ([regex]::Escape("native:$rv") + '[\s\S]{0,80}Ribbon')) { throw "nativeSceneFor does not route $rv to its scene" }
    if ($main -notmatch ([regex]::Escape("native:$rv") + '[\s\S]{0,80}Ribbon')) { throw "Main runtime map does not route $rv to its scene" }
}

# 5. every native scene actually instantiates (offscreen, 4s each, then killed).
foreach ($scene in @('ArenaNight', 'GildedRain', 'RibbonMotion', 'RibbonSunset', 'RibbonDusk', 'RibbonEmber')) {
    $errFile = Join-Path $env:TEMP "$($scene)_err.txt"
    $p = Start-Process -FilePath $qmlExe -ArgumentList @((Join-Path $root "qml/wallpapers/$scene.qml")) `
            -PassThru -RedirectStandardError $errFile -WindowStyle Hidden
    Start-Sleep -Seconds 4
    if ($p.HasExited -and $p.ExitCode -ne 0) { throw "$scene failed to instantiate (exit $($p.ExitCode))" }
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
    $errTxt = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    if ($errTxt -match 'error') { throw "$scene instantiation errors: $errTxt" }
}

Write-Host 'test_wallpaper_suite_p0: PASS (logic + paging UI + SFW gates + native arena + gilded rain + captured-motion ribbons)'

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

# 4. The picker renders only the Colosseum shelf above Wallhaven, with no scene routes.
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
if ($ui -notmatch 'text: "Colosseum"' -or $ui -notmatch 'model: WallpaperApi\.colosseumPicks\(\)') {
    throw 'WallpaperSearch lost its Colosseum shelf'
}
if ("$api`n$ui`n$main" -match 'Colosseum Animated|Colosseum Native|KDE Plasma"|nativeAnimatedPicks|nativeStaticPicks|nativePicks|kdePicks|nativeSceneFor|nativeWallpaperFile|wallpaperIsNative') {
    throw 'A retired wallpaper shelf or native-scene route crept back'
}
if ($main -notmatch 'wallpaperSource = Wallpapers\.imageSource\(pick,') {
    throw 'Saved wallpaper picks must use the shared source resolver'
}
if ($api -match 'function (houseDefaultPick|capturedMotionPicks|osPicks)|native:(arena-night|gilded-rain|facet|ribbon-)') {
    throw 'A retired wallpaper pick crept back into WallpaperApi'
}
if (Test-Path (Join-Path $root 'qml/wallpapers')) {
    $retiredFiles = Get-ChildItem (Join-Path $root 'qml/wallpapers') -File -Recurse
    if ($retiredFiles) { throw 'Retired native wallpaper scene files are still on disk' }
}
foreach ($name in @('silk', 'folds', 'tide')) {
    foreach ($suffix in @('.jpg', '-thumb.jpg')) {
        if (!(Test-Path (Join-Path $root "assets/wallpaper/colosseum-$name$suffix") -PathType Leaf)) {
            throw "Missing Colosseum wallpaper file: colosseum-$name$suffix"
        }
    }
}

Write-Host 'test_wallpaper_suite_p0: PASS (logic + paging UI + SFW gates + Colosseum shelf and retired routes)'

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

# 3. the API stays keyless and two-source with the SFW gates pinned.
$api = Get-Content (Join-Path $root 'qml/WallpaperApi.js') -Raw
if ($api -notmatch 'purity=100') { throw 'Wallhaven SFW purity gate lost' }
if ($api -notmatch 'rating:s') { throw 'Konachan SFW rating gate lost' }
if ($api -match 'api_key|apikey|token=') { throw 'a keyed source crept into the keyless wallpaper lane' }

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

# 4b. Gilded Rain (2026-07-19): second native living wallpaper, same gates as the arena.
$rain = Get-Content (Join-Path $root 'qml/wallpapers/GildedRain.qml') -Raw
if ($rain -notmatch 'property bool running') { throw 'GildedRain lost its running gate - it could never freeze' }
if ($rain -match 'Canvas\s*\{') { throw 'GildedRain must stay scene-graph work, never per-frame Canvas painting' }
if ($api -notmatch 'native:gilded-rain') { throw 'WallpaperApi lost gilded-rain from nativePicks' }
# every native route must resolve to a scene through the one shared map (no more hardcoding).
if ($api -notmatch 'function nativeSceneFor') { throw 'WallpaperApi lost the single nativeSceneFor scene map' }
if ($api -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'nativeSceneFor does not route gilded-rain to its scene' }
if ($main -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'Main runtime map does not route gilded-rain to its scene' }

# 5. both native scenes actually instantiate (offscreen, 4s each, then killed).
foreach ($scene in @('ArenaNight', 'GildedRain')) {
    $errFile = Join-Path $env:TEMP "$($scene)_err.txt"
    $p = Start-Process -FilePath $qmlExe -ArgumentList @((Join-Path $root "qml/wallpapers/$scene.qml")) `
            -PassThru -RedirectStandardError $errFile -WindowStyle Hidden
    Start-Sleep -Seconds 4
    if ($p.HasExited -and $p.ExitCode -ne 0) { throw "$scene failed to instantiate (exit $($p.ExitCode))" }
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
    $errTxt = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    if ($errTxt -match 'error') { throw "$scene instantiation errors: $errTxt" }
}

Write-Host 'test_wallpaper_suite_p0: PASS (logic + paging UI + SFW gates + native arena + gilded rain)'

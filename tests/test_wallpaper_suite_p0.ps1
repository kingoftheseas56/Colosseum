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

# 4c. KDE Plasma shelf (2026-07-20): real OS-desktop wallpapers under a free
# licence (CC-BY-SA-4.0 / LGPLv3), remote via jsDelivr through the keyless wsrv.nl
# proxy (no repo bloat, no api key). Each pick carries its artist for on-tile credit.
if ($api -notmatch 'function kdePicks') { throw 'WallpaperApi lost the KDE Plasma shelf' }
if ($api -notmatch 'plasma-workspace-wallpapers') { throw 'KDE picks must originate from the KDE wallpapers repo' }
if ($api -notmatch 'wsrv\.nl') { throw 'KDE picks must proxy through wsrv.nl' }
if ($api -notmatch 'cdn\.jsdelivr\.net') { throw 'KDE picks must originate from the jsDelivr CDN' }
if ($api -notmatch 'artist:') { throw 'KDE picks must carry an artist for credit' }
if ($ui -notmatch '"KDE Plasma"') { throw 'the picker lost its KDE Plasma shelf' }
if ($ui -notmatch 'WallpaperApi\.kdePicks\(\)') { throw 'the shelf must render kdePicks()' }
if ($ui -notmatch 'kdeTile\.modelData\.artist') { throw 'the KDE tile must show the artist' }
# THIRD_PARTY_NOTICES records the licences/attribution.
if (!(Test-Path (Join-Path $root 'THIRD_PARTY_NOTICES.md'))) { throw 'THIRD_PARTY_NOTICES.md (KDE attribution) is missing' }
# Captured Motion was pulled 2026-07-20 (Microsoft-copyrighted) - guard it stays out of the picker API/UI.
if ($api -match 'function houseDefaultPick|function capturedMotionPicks') { throw 'a Captured Motion picker function crept back' }
if ($api -match 'function osPicks') { throw 'the old OS-defaults shelf (osPicks) must be gone' }

# 4b. Gilded Rain (2026-07-19): second native living wallpaper, same gates as the arena.
$rain = Get-Content (Join-Path $root 'qml/wallpapers/GildedRain.qml') -Raw
if ($rain -notmatch 'property bool running') { throw 'GildedRain lost its running gate - it could never freeze' }
if ($rain -match 'Canvas\s*\{') { throw 'GildedRain must stay scene-graph work, never per-frame Canvas painting' }
if ($api -notmatch 'native:gilded-rain') { throw 'WallpaperApi lost gilded-rain from nativePicks' }
# every native route must resolve to a scene through the one shared map (no more hardcoding).
if ($api -notmatch 'function nativeSceneFor') { throw 'WallpaperApi lost the single nativeSceneFor scene map' }
if ($api -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'nativeSceneFor does not route gilded-rain to its scene' }
if ($main -notmatch 'native:gilded-rain[\s\S]{0,60}GildedRain\.qml') { throw 'Main runtime map does not route gilded-rain to its scene' }

# 4d. The QML-drawn Captured Motion recreations (RibbonMotion + variants) were
# pulled 2026-07-20 - both the Claude and Codex passes fell short of the real
# thing (and the real Captured Motion image shelf was pulled too, Microsoft-
# copyrighted). Guard that none of the ribbon wiring or scene files creep back.
foreach ($rv in @('ribbon-sunset', 'ribbon-dusk', 'ribbon-ember', 'RibbonMotion', 'RibbonSunset', 'RibbonDusk', 'RibbonEmber')) {
    if ($api -match [regex]::Escape($rv)) { throw "the pulled QML ribbon wallpaper ($rv) crept back into WallpaperApi" }
    if ($main -match [regex]::Escape($rv)) { throw "the pulled QML ribbon wallpaper ($rv) crept back into Main" }
}
foreach ($rf in @('RibbonMotion', 'RibbonSunset', 'RibbonDusk', 'RibbonEmber')) {
    if (Test-Path (Join-Path $root "qml/wallpapers/$rf.qml")) { throw "the pulled QML ribbon scene file ($rf.qml) is still on disk" }
}

# 4e. AuroraFlow (2026-07-20): a living gradient wallpaper ported from a real KDE
# Plasma QML wallpaper plugin (LGPL-2.1), KDE deps stripped. Scene-graph, freeze-gated.
$aurora = Get-Content (Join-Path $root 'qml/wallpapers/AuroraFlow.qml') -Raw
if ($aurora -notmatch 'property bool running') { throw 'AuroraFlow lost its running gate - it could never freeze' }
if ($aurora -match 'Canvas\s*\{') { throw 'AuroraFlow must stay scene-graph work, never per-frame Canvas painting' }
if ($aurora -notmatch 'import QtQuick.Shapes') { throw 'AuroraFlow must be built from Qt Quick Shapes' }
if ($api -notmatch 'native:aurora-flow[\s\S]{0,80}AuroraFlow\.qml') { throw 'nativeSceneFor does not route aurora-flow to its scene' }
if ($main -notmatch 'native:aurora-flow[\s\S]{0,80}AuroraFlow\.qml') { throw 'Main runtime map does not route aurora-flow to its scene' }
if ($api -notmatch '"Aurora Flow"') { throw 'nativePicks lost the Aurora Flow tile' }
# the ported scene's provenance/licence must be recorded.
$notices = Get-Content (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Raw
if ($notices -notmatch 'AuroraFlow') { throw 'THIRD_PARTY_NOTICES.md must record the AuroraFlow port provenance' }

# 4f. Still QML mesh-gradient wallpapers (2026-07-20): our own designs, Qt Quick
# Shapes, NO animation. These are the "normal QML wallpapers" - static by contract.
$mesh = Get-Content (Join-Path $root 'qml/wallpapers/MeshGradient.qml') -Raw
if ($mesh -notmatch 'property int variant') { throw 'MeshGradient lost its variant selector' }
if ($mesh -match 'Canvas\s*\{') { throw 'MeshGradient must stay scene-graph work, never per-frame Canvas painting' }
if ($mesh -notmatch 'import QtQuick.Shapes') { throw 'MeshGradient must be built from Qt Quick Shapes' }
if ($mesh -match 'SequentialAnimation|NumberAnimation|ColorAnimation') { throw 'MeshGradient must stay STILL (no animation) - it is a normal QML wallpaper' }
foreach ($mv in @('mesh-twilight', 'mesh-ember', 'mesh-mint')) {
    if ($api -notmatch [regex]::Escape("native:$mv")) { throw "nativePicks lost the $mv wallpaper" }
    if ($api -notmatch ([regex]::Escape("native:$mv") + '[\s\S]{0,80}Mesh')) { throw "nativeSceneFor does not route $mv to its scene" }
    if ($main -notmatch ([regex]::Escape("native:$mv") + '[\s\S]{0,80}Mesh')) { throw "Main runtime map does not route $mv to its scene" }
}

# 4g. Facet (2026-07-20): a still geometric QML wallpaper (Opal-spirit triangular
# lattice + sweeping glow band). Our own design, Qt Quick Shapes, NO animation.
$facet = Get-Content (Join-Path $root 'qml/wallpapers/Facet.qml') -Raw
if ($facet -match 'Canvas\s*\{') { throw 'Facet must stay scene-graph work, never per-frame Canvas painting' }
if ($facet -notmatch 'import QtQuick.Shapes') { throw 'Facet must be built from Qt Quick Shapes' }
if ($facet -match 'SequentialAnimation|NumberAnimation|ColorAnimation') { throw 'Facet must stay STILL (no animation)' }
if ($api -notmatch 'native:facet[\s\S]{0,80}Facet\.qml') { throw 'nativeSceneFor does not route facet to its scene' }
if ($main -notmatch 'native:facet[\s\S]{0,80}Facet\.qml') { throw 'Main runtime map does not route facet to its scene' }
if ($api -notmatch '"Facet"') { throw 'nativePicks lost the Facet tile' }

# 4h. LowPoly (2026-07-20): our first shader wallpaper - an ORIGINAL GLSL fragment
# shader run through a ShaderEffect (slow-morphing low-poly). Freeze-gated.
$lp = Get-Content (Join-Path $root 'qml/wallpapers/LowPoly.qml') -Raw
if ($lp -notmatch 'property bool running') { throw 'LowPoly lost its running gate - it could never freeze' }
if ($lp -notmatch 'ShaderEffect') { throw 'LowPoly must render through a ShaderEffect' }
if ($lp -notmatch 'FrameAnimation') { throw 'LowPoly must drive iTime from a freeze-gated FrameAnimation clock' }
if ($lp -notmatch 'lowpoly\.frag\.qsb') { throw 'LowPoly must reference the compiled shader' }
# the compiled shader + its GLSL source must be committed (QML loads the .qsb at runtime).
if (!(Test-Path (Join-Path $root 'qml/wallpapers/shaders/lowpoly.frag.qsb'))) { throw 'the compiled shader (lowpoly.frag.qsb) is missing' }
if (!(Test-Path (Join-Path $root 'qml/wallpapers/shaders/lowpoly.frag'))) { throw 'the shader source (lowpoly.frag) is missing' }
if ($api -notmatch 'native:lowpoly[\s\S]{0,80}LowPoly\.qml') { throw 'nativeSceneFor does not route lowpoly to its scene' }
if ($main -notmatch 'native:lowpoly[\s\S]{0,80}LowPoly\.qml') { throw 'Main runtime map does not route lowpoly to its scene' }
if ($api -notmatch '"Low Poly"') { throw 'nativePicks lost the Low Poly tile' }

# 5. every native scene actually instantiates (offscreen, 4s each, then killed).
foreach ($scene in @('ArenaNight', 'GildedRain', 'AuroraFlow', 'MeshGradient', 'MeshTwilight', 'MeshEmber', 'MeshMint', 'Facet', 'LowPoly')) {
    $errFile = Join-Path $env:TEMP "$($scene)_err.txt"
    $p = Start-Process -FilePath $qmlExe -ArgumentList @((Join-Path $root "qml/wallpapers/$scene.qml")) `
            -PassThru -RedirectStandardError $errFile -WindowStyle Hidden
    Start-Sleep -Seconds 4
    if ($p.HasExited -and $p.ExitCode -ne 0) { throw "$scene failed to instantiate (exit $($p.ExitCode))" }
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
    $errTxt = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    if ($errTxt -match 'error') { throw "$scene instantiation errors: $errTxt" }
}

Write-Host 'test_wallpaper_suite_p0: PASS (logic + paging UI + SFW gates + native arena + gilded rain; QML ribbon recreations removed)'

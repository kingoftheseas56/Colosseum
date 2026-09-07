# Cosmere universe — native/wiring contract. Unlike the payload-backed universes, the
# Cognitive Atlas self-sources from CosmereApi + Universes.js, so it carries NO bundled
# universe.json and NO UniverseExtApi FILES entry. This guards the restore against silent
# regressions in either direction.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Need([string]$path, [string]$needle, [string]$message) {
    if (-not (Test-Path $path)) { throw "Missing file: $path" }
    if ((Get-Content $path -Raw) -notlike "*$needle*") { throw $message }
}
function Lack([string]$path, [string]$needle, [string]$message) {
    if ((Get-Content $path -Raw) -like "*$needle*") { throw $message }
}

$main    = Join-Path $root 'qml\Main.qml'
$page    = Join-Path $root 'qml\CosmereUniversePage.qml'
$store   = Join-Path $root 'native\engine\ExtensionsStore.cpp'
$api     = Join-Path $root 'qml\UniverseExtApi.js'
$mainCpp = Join-Path $root 'native\main.cpp'

# Seeded as a house-default universe extension so it reaches the Home roster.
Need $store 'com.colosseum.universe.cosmere' 'Cosmere must be a seeded house-default universe extension.'
# The bespoke page exists and is routed by extension id.
Need $page 'THE COGNITIVE ATLAS' 'The Cognitive Atlas page must be present.'
Need $main 'com.colosseum.universe.cosmere' 'Main must route the Cosmere extension id.'
Need $main 'CosmereUniversePage.qml' 'Main must open the bespoke Cosmere page.'
Need $main 'item.bookRequested.connect(win.openBook)' 'Cosmere books must open into the reader.'
# The page carries the loader contract it needs and self-sources.
Need $page 'property string extensionId' 'The page must accept extensionId so the loader assignment succeeds.'
Need $page 'signal fullscreenRequested()' 'The page must expose the shell fullscreen verb.'
Need $page 'CosmereApi.js' 'The Atlas must self-source through CosmereApi.'
# Dev boot route, mirroring the other universes.
Need $mainCpp 'COLOSSEUM_OPEN_UNIVERSE' 'Native boot harness must expose the universe dev route.'
Need $main 'devUniverse === "cosmere"' 'Main must honor COLOSSEUM_OPEN_UNIVERSE=cosmere.'
# Self-sourced: it must NOT masquerade as a payload-backed universe.
Lack $api 'com.colosseum.universe.cosmere' 'Cosmere self-sources; it must not be registered as a payload universe.'
if (Test-Path (Join-Path $root 'assets\universes\cosmere.json')) {
    throw 'Cosmere must not ship a bundled payload; it self-sources from CosmereApi/Universes.js.'
}

Write-Host 'COSMERE_NATIVE_CONTRACT_OK'

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Need([string]$path,[string]$needle,[string]$message) {
    if (-not (Test-Path $path)) { throw "Missing file: $path" }
    $text = Get-Content $path -Raw
    if ($text -notlike "*$needle*") { throw $message }
}
$api = Join-Path $root 'qml\UniverseExtApi.js'
$main = Join-Path $root 'qml\Main.qml'
$galaxy = Join-Path $root 'qml\GalaxyUniversePage.qml'
$adapter = Join-Path $root 'qml\UniverseGalleryCard.qml'
$store = Join-Path $root 'native\engine\ExtensionsStore.cpp'
$payload = Join-Path $root 'assets\universes\star-wars.json'
Need $api 'com.colosseum.universe.starwars' 'Star Wars payload must be registered in UniverseExtApi.'
Need $store 'com.colosseum.universe.starwars' 'Star Wars must be a seeded universe extension.'
Need $main 'GalaxyUniversePage.qml' 'Main must route Star Wars to the bespoke galaxy page.'
Need $galaxy 'high-republic-screen' 'Galaxy page must map verified High Republic sections.'
Need $galaxy 'new-republic-first-order-comics' 'Galaxy page must preserve cross-era comic semantics.'
Need $galaxy 'Across the Eras' 'Galaxy page must expose the cross-era destination.'
Need $galaxy 'Beyond Canon' 'Galaxy page must expose the adjacent-continuity destination.'
Need $adapter 'CataloguePosterCard' 'Universe gallery adapter must reuse the shared catalogue poster card.'
Need $adapter 'visualProfile: "gallery"' 'Universe media must use the current gallery profile.'
if (-not (Test-Path $payload)) { throw 'Missing bundled Star Wars payload.' }
$worlds = @('valo.jpg','coruscant.jpg','mustafar.jpg','hoth.jpg','nevarro.jpg','jakku.jpg','ahch-to.jpg')
foreach ($name in $worlds) {
    $p = Join-Path $root ("assets\universes\star-wars\" + $name)
    if (-not (Test-Path $p)) { throw "Missing Star Wars environment: $name" }
}
$g = Get-Content $galaxy -Raw
if ($g -like '*UniverseTile*') { throw 'Star Wars page must not use numbered UniverseTile cards.' }
if ($g -like '*Saga.loadGalaxy*') { throw 'Star Wars page must use the verified extension payload, not legacy Saga.loadGalaxy.' }
Need (Join-Path $root 'native\main.cpp') 'COLOSSEUM_OPEN_UNIVERSE' 'Native boot harness must expose a universe dev route.'
Need $main 'DevOpenUniverse' 'Main must honor the universe dev boot route.'
Write-Host 'STAR_WARS_NATIVE_CONTRACT_OK'

# Your Collection arc — wiring contracts (SHAPE, not behavior; behavior = harness).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) {
    if ($hay -notlike "*$needle*") { Write-Host "FAIL: $msg"; exit 1 }
}

$main = Read-File "native/main.cpp"
Assert-Contains $main '#include "CollectionStore.h"' "main.cpp includes CollectionStore.h"
Assert-Contains $main 'setContextProperty(QStringLiteral("Collection")' "Collection registered as context property"

# The behavioral harness must exist and pass.
$harness = Join-Path $root "native\build-msvc\collection_store_harness.exe"
if (-not (Test-Path $harness)) { Write-Host "FAIL: build collection_store_harness first"; exit 1 }
& $harness | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: collection_store_harness red"; exit 1 }

$libBtn = Read-File "qml/LibraryButton.qml"
Assert-Contains $libBtn 'Collection.has(world, String(entry.id))' "LibraryButton reads live saved state"
Assert-Contains $libBtn 'Collection.revision' "LibraryButton names revision for reactivity"
$crow = Read-File "qml/ContinueRow.qml"
Assert-Contains $crow 'forgetHandler' "ContinueRow grew the forgetHandler seam"
Assert-Contains $crow 'Progress.forget(modelData.kind, modelData.id)' "default Progress wiring survives"
$mainQml = Read-File "qml/Main.qml"
Assert-Contains $mainQml 'function openCollectionEntry(' "Main.qml routes collection clicks"
Assert-Contains $mainQml 'collectionOpenRequested.connect(win.openCollectionEntry)' "world loaders connect the signal"

$ts = Read-File "qml/TheatreSeries.qml"
Assert-Contains $ts 'function collectionEntry()' "TheatreSeries snapshots its identity"
Assert-Contains $ts 'LibraryButton {' "TheatreSeries carries the button"

$tw = Read-File "qml/TheatreWorld.qml"
Assert-Contains $tw 'Collection.items("theatre")' "Theatre world carries the Collection row"
Assert-Contains $tw 'signal collectionOpenRequested' "Theatre world bubbles collection clicks"

$crow2 = Read-File "qml/ContinueRow.qml"
Assert-Contains $crow2 'property bool showSeeAll' "ContinueRow can hide the see-all chevron"
Assert-Contains $crow2 'navigable: cont.showSeeAll' "chevron honors showSeeAll"
Assert-Contains $tw 'showSeeAll: false' "Theatre Collection row hides the dead chevron"

$bb = Read-File "qml/BiblioBook.qml"
Assert-Contains $bb 'world: "biblio"' "BiblioBook button is live"
$bw = Read-File "qml/BiblioWorld.qml"
Assert-Contains $bw 'Collection.items("biblio")' "Biblio world carries the Collection row"

Write-Host "test_collection_p0 PASSED"

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'name: "Cosmere"' "Universes.js must carry Cosmere."
Assert-Contains $udb 'category: "cosmere"' "Cosmere must select its own template."
Assert-Contains $udb 'cosmereStarters:' "Cosmere must curate newcomer portals."
Assert-Contains $udb 'cosmereWorlds:' "Cosmere must curate planetary systems."

$api = Read-File "qml/CosmereApi.js"
Assert-Contains $api '.import "BiblioApi.js" as Biblio' "Cosmere books must use the Biblio lane."
Assert-Contains $api 'function snapshot(' "CosmereApi must expose the pure ordered snapshot seam."
Assert-Contains $api 'function loadAtlas(' "CosmereApi must expose the live atlas loader."
Assert-Contains $api 'Biblio.lookupBook' "Every curated title must resolve through Biblio."

$page = Read-File "qml/CosmereUniversePage.qml"
Assert-Contains $page 'signal bookRequested(var book)' "The atlas must expose the Biblio book verb."
Assert-Contains $page 'root.bookRequested(book)' "Every live gate must emit a full book object."
Assert-Contains $page 'THE COGNITIVE ATLAS' "The page must carry its own atlas identity."
Assert-Contains $page 'OPEN IN BIBLIO' "World gates must name their real destination."
Assert-Contains $page 'activeFocusOnTab: true' "Book gates must be keyboard focusable."
Assert-Contains $page 'Keys.onReturnPressed' "Book gates must activate from the keyboard."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'category === "cosmere"' "Main must recognize the Cosmere category."
Assert-Contains $main 'CosmereUniversePage.qml' "Cosmere must open its dedicated template."
Assert-Contains $main 'item.bookRequested.connect(win.openBook)' "Cosmere books must open Biblio detail."

$harness = Join-Path $PSScriptRoot "cosmere_api_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Cosmere API harness failed (exit $LASTEXITCODE)" }

Write-Host "cosmere universe p0: OK"

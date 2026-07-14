$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    throw "qml.exe not found at $qmlExe"
}

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (!$text.Contains($needle)) { throw $message }
}

function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { throw $message }
}

$main = Read-RepoFile "qml/Main.qml"
$world = Read-RepoFile "qml/TankobanWorld.qml"
$ledger = Read-RepoFile "qml/ComicDbLedger.qml"

Assert-NotContains $main 'import "comics_db.gen.js" as ComicsDbData' `
    "Main.qml must not parse the generated catalog at root startup."
Assert-NotContains $main 'ComicsDb.setData(ComicsDbData.data)' `
    "Main.qml must not ingest the catalog at root startup."
Assert-Contains $world 'import "comics_db.gen.js" as ComicsDbData' `
    "TankobanWorld.qml must own the lazy generated-catalog import."
Assert-Contains $world 'ComicsDb.setData(ComicsDbData.data)' `
    "TankobanWorld.qml must ingest the catalog when its Loader creates the world."
Assert-Contains $ledger 'property bool   hasSource: !!ed.modelData.available && postUrl.length > 0' `
    "Ledger availability must require a verified GetComics post."
Assert-Contains $ledger 'if (typeof Comics === "undefined" || !chId.length || !canAcquire) return' `
    "The ledger primary action must reject unavailable editions."
Assert-NotContains $ledger 'downloadIssueTorrent' `
    "The ledger must never auto-pick a torrent source."
Assert-Contains $ledger 'ed.modelData.display_title || ed.modelData.title' `
    "Ledger rows and download labels must prefer the exact-ISBN canonical edition name."

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comics_catalog_logic_harness.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $output -notlike "*COMICS_CATALOG_OK 688*") {
    throw "Comics catalog logic harness failed (exit $LASTEXITCODE):`n$output"
}

Write-Host "comics catalog v1: OK"

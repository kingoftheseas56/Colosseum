$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (!$text.Contains($needle)) { throw $message }
}

$top = Read-RepoFile "qml/TrendingTop10.qml"
$world = Read-RepoFile "qml/TankobanWorld.qml"

Assert-Contains $top 'signal exploreClicked()' `
    "TrendingTop10 must expose the existing Explore label as an event."
Assert-Contains $top 'onMoreClicked: top10.exploreClicked()' `
    "TrendingTop10 Explore label must emit exploreClicked."
Assert-Contains $world 'signal comicCatalogRequested(var rows)' `
    "TankobanWorld must expose the full-catalog route."
Assert-Contains $world 'items: tanko.comicRows.slice(0, 10)' `
    "Tankoban home Top Comics must display exactly ten resolved rows."
Assert-Contains $world 'var topComics = tanko.comicRows.slice(0, 10)' `
    "Top Comics clicks must resolve against the same ten-row slice."
Assert-Contains $world 'tanko.comicCatalogRequested(tanko.comicRows)' `
    "Top Comics Explore must receive the unsliced full catalog."

Write-Host "top comics explore contracts: OK"

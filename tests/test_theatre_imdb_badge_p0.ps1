$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$qml = Get-Content -Raw -LiteralPath (Join-Path $root 'qml\TheatreSeries.qml')

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Assert-Absent($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) { throw $Message }
}

Assert-Contains $qml 'id:\s*imdbRatingBadge' `
    'Theatre hero must expose a stable IMDb rating group.'
Assert-Contains $qml 'id:\s*imdbRatingBadge[\s\S]{0,180}visible:\s*page\.rating\.length' `
    'IMDb badge visibility must remain driven by rating availability.'
Assert-Contains $qml 'id:\s*imdbPlaque[\s\S]{0,260}color:\s*"#F5C518"' `
    'IMDb plaque must use IMDb yellow.'
Assert-Contains $qml 'id:\s*imdbPlaque[\s\S]{0,500}text:\s*"IMDb"' `
    'IMDb plaque must identify the rating source.'
Assert-Contains $qml 'id:\s*imdbRatingValue[\s\S]{0,220}text:\s*page\.rating' `
    'IMDb rating value must remain data-driven.'
Assert-Contains $qml 'id:\s*imdbRatingValue[\s\S]{0,300}color:\s*theme\.ink' `
    'IMDb numeric value must use neutral metadata ink.'
Assert-Absent $qml 'text:\s*"\* "\s*\+\s*page\.rating' `
    'The hard-coded star placeholder is retired.'

Write-Host 'TheatreSeries IMDb badge contract OK.'


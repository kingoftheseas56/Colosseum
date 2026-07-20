$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$qml = Get-Content -Raw -LiteralPath (Join-Path $root 'qml\TheatreSeries.qml')

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Assert-Absent($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) { throw $Message }
}

# Approved 2026-07-20 balanced ledger: one chronological virtualized list, no card/strip fork.
Assert-Contains $qml 'id:\s*episodeLedgerHeader' 'Episode ledger must expose its stable summary header.'
Assert-Contains $qml 'id:\s*episodeNumberRail' 'Episode numbers must live in a dedicated ledger rail.'
Assert-Contains $qml 'id:\s*nextUpRail' 'The in-place next-up row must have a gold rail.'
Assert-Contains $qml 'id:\s*rowActions' 'Episode actions must occupy a stable right column.'
Assert-Contains $qml 'property\s+int\s+compactRowHeight:\s*104' 'Normal ledger rows must be 104px.'
Assert-Contains $qml 'property\s+int\s+nextRowHeight:\s*148' 'The next-up ledger row must expand to 148px.'
Assert-Contains $qml 'height:\s*ep\.nextUp\s*\?\s*episodeList\.nextRowHeight\s*:\s*episodeList\.compactRowHeight' 'Next-up must expand in place without model reordering.'
Assert-Contains $qml 'orientation:\s*ListView\.Vertical' 'Episode ledger must stay vertical.'
Assert-Contains $qml 'spacing:\s*0' 'Ledger rows must meet on hairline separators.'

# Semantic colors and real Lucide controls.
Assert-Contains $qml 'property\s+color\s+watchedInk:\s*"#76b8aa"' 'Watched state must use the approved quiet teal.'
Assert-Contains $qml 'PlayerIcon\s*\{[\s\S]{0,220}kind:\s*"play"' 'Rows must expose a real Lucide play action.'
Assert-Contains $qml 'PlayerIcon\s*\{[\s\S]{0,260}kind:\s*epDl\.onDisk\s*\?\s*"check"' 'Download state must use semantic Lucide icons.'
Assert-Contains $qml 'id:\s*seasonDownloadAction[\s\S]{0,500}border\.color:\s*theme\.edge' 'Season download must be a neutral secondary action.'

# Existing behavior remains wired to the same source/download paths.
Assert-Contains $qml 'page\.sheetEpisode\s*=\s*ep\.modelData[\s\S]{0,500}sources\.show\("series"' 'Row/play action must retain the source-sheet play flow.'
Assert-Contains $qml 'page\.pendingDownloadEpisode\s*=\s*ep\.modelData[\s\S]{0,650}"download"' 'Row download must retain the source-picker download flow.'
Assert-Contains $qml 'onClicked:\s*page\.openSeasonPicker\(\)' 'Season download must retain the pack picker.'

# The rejected control languages may not survive in the production surface.
Assert-Absent $qml 'property\s+string\s+episodeLayout' 'The list/strip state is retired.'
Assert-Absent $qml '\\u2630|\\u25ad' 'Unicode list/strip icons are retired.'
Assert-Absent $qml 'text:\s*"\\u2713"' 'Watched state must not use a text check glyph.'
Assert-Absent $qml 'text:\s*epDl\.onDisk\s*\?' 'Download state must not use text glyphs.'

Write-Host 'TheatreSeries balanced episode ledger contract OK.'

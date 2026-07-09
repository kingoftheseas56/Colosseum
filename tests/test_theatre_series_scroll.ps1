$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$qmlPath = Join-Path $root 'qml\TheatreSeries.qml'
$qml = Get-Content -Raw -LiteralPath $qmlPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-NotContains($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) {
        throw $Message
    }
}

Assert-Contains $qml 'ListView\s*\{\s*[\s\S]*id:\s*episodeList[\s\S]*model:\s*page\.episodes' `
    'TheatreSeries episodes must be rendered by a virtualized episodeList ListView.'
Assert-Contains $qml 'ScrollBar\.vertical:\s*HouseScrollBar\s*\{' `
    'TheatreSeries episode list must expose a vertical scrollbar.'
Assert-NotContains $qml '(?m)^\s*Repeater\s*\{\s*\r?\n\s*model:\s*page\.episodes\b' `
    'TheatreSeries must not render all episodes through a Repeater.'
Assert-Contains $qml 'function\s+defaultSeason\(\)\s*\{[\s\S]*seasons\[seasons\.length - 1\]' `
    'TheatreSeries should default multi-season shows to the newest season.'
Assert-Contains $qml 'source:\s*ep\.modelData\.thumbnail\s*\?\s*ep\.modelData\.thumbnail\s*:\s*page\.sourceBackdrop\(\)' `
    'TheatreSeries episode thumbnails should fall back to series art.'
Assert-Contains $qml 'property\s+string\s+episodeLayout:\s*"list"' `
    'TheatreSeries should expose a list/strip episode layout state.'
Assert-Contains $qml 'orientation:\s*page\.episodeLayout\s*===\s*"strip"\s*\?\s*ListView\.Horizontal\s*:\s*ListView\.Vertical' `
    'TheatreSeries episode ListView should toggle between list and strip orientations.'
Assert-Contains $qml 'function\s+jumpToEpisodeNumber\(number\)' `
    'TheatreSeries should provide a direct episode jumper.'
Assert-Contains $qml 'positionViewAtIndex\(index,\s*ListView\.Beginning\)' `
    'TheatreSeries episode jumper should seek the virtualized ListView without rendering all rows.'
Assert-Contains $qml 'model:\s*Math\.ceil\(page\.episodes\.length / 50\)' `
    'TheatreSeries episode jumper should expose Harbor-style 50-episode range shortcuts.'

Write-Host 'TheatreSeries episode scrolling structure OK.'

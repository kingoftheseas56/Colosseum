$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$qmlPath = Join-Path $root 'qml\TheatreSeries.qml'
$qml = Get-Content -Raw -LiteralPath $qmlPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Assert-NotContains($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) { throw $Message }
}

Assert-Contains $qml 'Item\s*\{\s*id:\s*episodeVirtualSpace[\s\S]*height:\s*page\.episodeContentHeight' `
    'TheatreSeries must reserve the full episode ledger height without realizing every row.'
Assert-Contains $qml 'Repeater\s*\{\s*id:\s*episodeWindowRepeater[\s\S]*model:\s*page\.episodeWindowModel' `
    'TheatreSeries must realize only the moving episode window.'
Assert-NotContains $qml 'height:\s*contentHeight\s*\r?\n\s*model:\s*page\.episodes' `
    'TheatreSeries must not restore the full-height ListView that realizes every episode.'
Assert-NotContains $qml '(?m)^\s*Repeater\s*\{\s*\r?\n\s*model:\s*page\.episodes\b' `
    'TheatreSeries must not render all episodes through a Repeater.'
Assert-Contains $qml 'property\s+int\s+nextUpEpisodeIndex:' `
    'TheatreSeries should cache the next-up index once per episode/progress revision.'
Assert-Contains $qml 'function\s+episodeOffsetForIndex\(index\)' `
    'TheatreSeries must compute episode coordinates without requiring a realized delegate.'
Assert-Contains $qml 'function\s+episodeIndexAtOffset\(offset\)' `
    'TheatreSeries must invert virtual-ledger coordinates without requiring delegates.'
Assert-Contains $qml 'property\s+int\s+episodeWindowStart:' `
    'TheatreSeries must derive a bounded visible-window start index.'
Assert-Contains $qml 'property\s+int\s+episodeWindowEnd:' `
    'TheatreSeries must derive a bounded visible-window end index.'
Assert-Contains $qml 'property\s+var\s+episodeWindowModel:' `
    'TheatreSeries must expose only the bounded episode slice to the delegate repeater.'
Assert-Contains $qml 'objectName:\s*"theatreSeriesScroll"' `
    'TheatreSeries must expose its page scroll surface to Lanista.'
Assert-Contains $qml 'liveEpisodeDelegateCount:\s*episodeWindowRepeater\.count' `
    'TheatreSeries must expose the live virtual delegate count for runtime verification.'
Assert-NotContains $qml 'page\.nextUpEpisodeId\(\)\s*===\s*page\.episodeStreamId\(modelData\)' `
    'Episode delegates must not rescan the full series to discover next-up state.'
Assert-Contains $qml 'function\s+defaultSeason\(\)\s*\{[\s\S]*for \(var i = 0; i < seasons\.length; i\+\+\)' `
    'TheatreSeries should default a fresh multi-season show to the FIRST numbered season.'
Assert-Contains $qml 'source:\s*ep\.modelData\.thumbnail\s*\?\s*ep\.modelData\.thumbnail\s*:\s*page\.sourceBackdrop\(\)' `
    'TheatreSeries episode thumbnails should fall back to series art.'
Assert-NotContains $qml 'property\s+string\s+episodeLayout' `
    'TheatreSeries should use one stable balanced-ledger layout.'
Assert-Contains $qml 'height:\s*ep\.nextUp\s*\?\s*page\.nextUpEpisodeRowHeight\s*:\s*page\.compactEpisodeRowHeight' `
    'TheatreSeries should expand next-up in chronological position.'
Assert-Contains $qml 'function\s+jumpToEpisodeNumber\(number\)' `
    'TheatreSeries should provide a direct episode jumper.'
Assert-Contains $qml 'page\.episodeVirtualContentY\s*\+\s*page\.episodeOffsetForIndex\(index\)' `
    'TheatreSeries episode jumper should seek the page-level virtual ledger.'
Assert-Contains $qml 'model:\s*Math\.ceil\(page\.episodes\.length / 50\)' `
    'TheatreSeries episode jumper should expose Harbor-style 50-episode range shortcuts.'

Write-Host 'TheatreSeries episode virtualization structure OK.'

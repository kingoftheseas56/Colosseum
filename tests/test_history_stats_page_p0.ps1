$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$page = Get-Content -Raw -LiteralPath (Join-Path $root 'qml/HistoryHighlightsStatsPage.qml')
$model = Get-Content -Raw -LiteralPath (Join-Path $root 'qml/HistoryStatsModel.js')
$main = Get-Content -Raw -LiteralPath (Join-Path $root 'qml/Main.qml')
$taskbar = Get-Content -Raw -LiteralPath (Join-Path $root 'qml/Taskbar.qml')
$back = Get-Content -Raw -LiteralPath (Join-Path $root 'qml/ShellBackPolicy.js')

$requiredPageTokens = @(
    'objectName: "historyHighlightsStatsPage"',
    'objectName: "historySectionTab"',
    'objectName: "highlightsSectionTab"',
    'objectName: "statsSectionTab"',
    'No activity yet',
    'trackerModel.historyDeliveryRows()',
    'modelData.syncLabel',
    'signal syncCenterRequested()'
)
foreach ($token in $requiredPageTokens) {
    if (-not $page.Contains($token)) { throw "Missing page contract token: $token" }
}
if (-not $model.Contains('return "Local only"')) {
    throw 'History model does not preserve the local-only receipt state'
}

foreach ($token in @('signal historyStatsClicked()', 'property bool historyStatsActive: false',
                      'objectName: "taskbarHistoryStats"', 'accessibleName: "History, highlights, and stats"')) {
    if (-not $taskbar.Contains($token)) { throw "Missing taskbar route token: $token" }
}

foreach ($token in @('id: historyStatsLayer', 'source: "HistoryHighlightsStatsPage.qml"',
                      'function openHistoryStatsPage()', 'function closeHistoryStatsPage()',
                      'onHistoryStatsClicked:', 'item.syncCenterRequested.connect(win.openSyncCenterPage)')) {
    if (-not $main.Contains($token)) { throw "Missing Main route token: $token" }
}

if (-not $back.Contains('if (on(s.historyStatsActive)) return "historyStats"')) {
    throw 'History page does not own Escape in ShellBackPolicy'
}

Write-Output 'PASS Arc 35 History/Highlights/Stats page contract'

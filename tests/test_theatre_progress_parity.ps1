$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$progress = Get-Content -Raw -LiteralPath (Join-Path $root "native\ProgressStore.h")
$player = Get-Content -Raw -LiteralPath (Join-Path $root "qml\PlayerPage.qml")
$main = Get-Content -Raw -LiteralPath (Join-Path $root "qml\Main.qml")
$series = Get-Content -Raw -LiteralPath (Join-Path $root "qml\TheatreSeries.qml")

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Contains $progress 'Q_INVOKABLE\s+QVariantMap\s+get\(const QString &kind, const QString &id\) const' `
    "ProgressStore must expose one-entry reads for per-episode detail state."
Assert-Contains $progress 'Q_INVOKABLE\s+int\s+lastSeason\(const QString &seriesId\) const' `
    "ProgressStore must persist the last selected season."
Assert-Contains $progress 'Q_INVOKABLE\s+void\s+rememberLastSeason\(const QString &seriesId, int season\)' `
    "ProgressStore must expose last-season writes."
Assert-Contains $progress 'id\.count\(QLatin1Char\('':''\)\)\s*>=\s*2' `
    "Series episode progress must be detected from tt:season:episode ids."
Assert-Contains $progress 'rec\.insert\(QStringLiteral\("watched"\), true\)' `
    "Finished series episodes should remain as watched markers instead of being dropped."
Assert-Contains $progress 'continueGroupKey' `
    "ProgressStore.recent must collapse episode entries to one Continue card per show."
Assert-Contains $progress 'seriesRootId' `
    "ProgressStore must derive a parent-show key from episode ids like tt123:1:2."
Assert-Contains $progress 'shouldPreferContinueCandidate' `
    "ProgressStore must prefer unfinished episodes over watched markers when deduping Continue."
Assert-Contains $progress 'return parts\.value\(0\)' `
    "Cinemeta series episodes must group by the base tt id."
Assert-Contains $progress "return parts\.value\(0\) \+ QLatin1Char\(':'\) \+ parts\.value\(1\)" `
    "Anime episode ids must group by provider plus series id."

Assert-Contains $player 'root\.mediaId\s*=\s*\(subType === "series" && subId\)\s*\?\s*subId' `
    "PlayerPage must record series progress under the exact episode id."
Assert-Contains $player '"subType": root\.subStreamType' `
    "PlayerPage resume payload must preserve stream type."
Assert-Contains $player '"subId": root\.subStreamId' `
    "PlayerPage resume payload must preserve stream id."
Assert-Contains $main 'r\.subType \|\| ""\s*,\s*r\.subId \|\| ""' `
    "Main resumeContinue must pass subtitle/episode identity back into player sessions."

Assert-Contains $series 'Progress\.get\("video", episodeStreamId\(v\)\)' `
    "TheatreSeries must read per-episode Progress entries."
Assert-Contains $series 'Progress\.lastSeason\(currentId\(\)\)' `
    "TheatreSeries should restore the saved season."
Assert-Contains $series 'Progress\.rememberLastSeason\(currentId\(\), activeSeason\)' `
    "TheatreSeries should save the selected season."
Assert-Contains $series 'function\s+nextUpEpisodeNumber\(\)' `
    "TheatreSeries should compute a next-up episode from watched progress."

Write-Host "Theatre progress parity structure OK."

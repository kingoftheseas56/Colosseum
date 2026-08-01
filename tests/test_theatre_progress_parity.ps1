$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$progress = Get-Content -Raw -LiteralPath (Join-Path $root "native\ProgressStore.h")
$player = Get-Content -Raw -LiteralPath (Join-Path $root "qml\PlayerPage.qml")
$main = Get-Content -Raw -LiteralPath (Join-Path $root "qml\Main.qml")
$series = Get-Content -Raw -LiteralPath (Join-Path $root "qml\TheatreSeries.qml")
$hosted = Get-Content -Raw -LiteralPath (Join-Path $root "qml\HostedPlayerPage.qml")

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

# Task 3 — keyless TMDB identity for hosted playback (VidKing).
Assert-Contains $series 'property int tmdbId' `
    "TheatreSeries must hold the resolved TMDB identity for hosted playback."
Assert-Contains $series 'tmdbId\s*=\s*0' `
    "TheatreSeries must reset tmdbId to zero before each item load."
Assert-Contains $series 'page\.tmdbId\s*=\s*Math\.max\(0,\s*Math\.floor\(Number\(meta\.moviedb_id\s*\|\|\s*meta\.tmdbId' `
    "TheatreSeries must read the resolved Cinemeta moviedb_id / tmdbId after loadMeta."
Assert-Contains $series '"tmdbId":\s*page\.tmdbId' `
    "Play-mode source asks must carry the TMDB id into the sheet's playback context."
Assert-Contains $series '"imdbId":\s*page\.currentId\(\)' `
    "Play-mode source asks must carry the imdb id into the sheet's playback context."

# Task 6 — hosted playback writes the same Progress payload shape as mpv, keyed by the
# existing Colosseum video id, so Continue Watching resumes VidKing.
Assert-Contains $hosted 'Progress\.recordSilent\(' `
    "HostedPlayerPage must persist the 5-second heartbeat silently."
Assert-Contains $hosted 'Progress\.record\(' `
    "HostedPlayerPage must write lifecycle progress with a notify."
Assert-Contains $hosted '"id":\s*(request|r)\.mediaId' `
    "Hosted progress must be keyed by the existing Colosseum video id."
Assert-Contains $hosted '"hostedPlayerId":\s*(request|r)\.providerId' `
    "Hosted resume metadata must mark the provider so Continue routes back to VidKing."
Assert-Contains $hosted '"position":\s*[a-zA-Z.]*(lastPosition|currentTime|position)' `
    "Hosted resume must carry the last playback position in seconds."

# Task 7 — Continue Watching routes a hosted entry back to VidKing BEFORE the torrent/local
# branches, and only while net.vidking.player is installed and enabled. The hosted Loader is
# separate from playerLayer and never changes usePlayer2.
Assert-Contains $main 'openHostedPlayerSession' `
    "Main must expose openHostedPlayerSession as the hosted entry point."
Assert-Contains $main '"contentKind":\s*"hosted-video"' `
    "Hosted sessions must declare contentKind hosted-video."
Assert-Contains $main 'hostedPlayerLayer' `
    "Main must declare a hostedPlayerLayer Loader beside playerLayer."
Assert-Contains $main 'function minimizeHostedPlayer' `
    "Main must declare minimizeHostedPlayer()."
Assert-Contains $main 'function closeHostedPlayerSession' `
    "Main must declare closeHostedPlayerSession()."
Assert-Contains $main 'hostedPlayerLayer\.active\s*=\s*false' `
    "Minimize/close must UNLOAD the hosted Loader so no warm iframe survives."
Assert-Contains $main 'r\.hostedPlayerId' `
    "resumeContinue must check resume.hostedPlayerId before localPath/infoHash."
Assert-Contains $main 'openTheatreSeries' `
    "A disabled/removed VidKing Continue must fall back to Theatre detail."
Assert-Contains $main 'reopenSources' `
    "Back to Sources must restore the original Sources context."

Write-Host "Theatre progress parity structure OK."

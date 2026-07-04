$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$sources = Get-Content (Join-Path $root "qml/SourcesSheet.qml") -Raw
$series = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

# Harbor parity P0: the player must receive the whole source set so it can retry/switch
# without kicking the user back to the detail page.
Assert-Matches $sources "signal\s+playRequested\([^\)]*var\s+streamCandidates" `
    "SourcesSheet must emit streamCandidates with the selected source."
Assert-Matches $series "signal\s+playRequested\([^\)]*var\s+streamCandidates" `
    "TheatreSeries must carry streamCandidates through its playRequested signal."
Assert-Matches $main "function\s+openMovieSession\([^\)]*streamCandidates" `
    "Main.openMovieSession must accept streamCandidates for in-player stream switching."
Assert-Matches $main "playTorrent\([^\)]*streamCandidates" `
    "Main must pass streamCandidates into PlayerPage.playTorrent."
Assert-Matches $player "function\s+playTorrent\([^\)]*streamCandidates" `
    "PlayerPage.playTorrent must accept streamCandidates."

# Harbor parity P0: the player must own recovery/switching state and actions.
Assert-Contains $player "property var streamCandidates" `
    "PlayerPage must store streamCandidates."
Assert-Contains $player "property int currentStreamIndex" `
    "PlayerPage must track the active stream index."
Assert-Contains $player "function retryCurrentStream" `
    "PlayerPage must expose retryCurrentStream()."
Assert-Contains $player "function pickAnotherStream" `
    "PlayerPage must expose pickAnotherStream()."
Assert-Contains $player "function handlePlaybackFailure" `
    "PlayerPage must route mpv failures through handlePlaybackFailure()."
Assert-Contains $player "property int streamWatchdogSeconds" `
    "PlayerPage must define a stream watchdog timeout."
Assert-Contains $player "Timer {" `
    "PlayerPage must include a timer-based stream watchdog."
Assert-Contains $player "handleStreamWatchdog" `
    "PlayerPage must route slow/stuck stream startup through handleStreamWatchdog()."
Assert-Contains $player "source did not start" `
    "PlayerPage watchdog failure should explain that the source did not start."
Assert-Contains $player "Retry stream" `
    "PlayerPage chrome must expose a retry action."
Assert-Contains $player "Pick another stream" `
    "PlayerPage chrome must expose a pick-another-stream action."

# Harbor parity P0: series playback must carry adjacent episode context into the player.
Assert-Matches $series "adjacentEpisodeContext\s*\(" `
    "TheatreSeries must build adjacent episode context for selected episodes."
Assert-Contains $series "target.context = adjacentEpisodeContext(v)" `
    "TheatreSeries adjacent targets must carry their own context for chained auto-next."
Assert-Contains $series "`"episodeQueue`": queue" `
    "TheatreSeries must pass an episode queue for chained player navigation."
Assert-Contains $series "`"episodeIndex`": idx" `
    "TheatreSeries must pass the current episode index for chained player navigation."
Assert-Matches $sources "property\s+var\s+playbackContext" `
    "SourcesSheet must retain playbackContext from TheatreSeries."
Assert-Matches $player "property\s+var\s+adjacentEpisodes" `
    "PlayerPage must store adjacent episode context."
Assert-Contains $player "function resolveAdjacentContext" `
    "PlayerPage must resolve adjacent episodes from queue/index context."
Assert-Contains $player "function goToAdjacentEpisode" `
    "PlayerPage must expose goToAdjacentEpisode()."
Assert-Contains $player "function maybeAutoNextEpisode" `
    "PlayerPage must auto-advance when a next episode exists."
Assert-Contains $player "Torrentio.loadStreams" `
    "PlayerPage auto-next must resolve the next episode's Torrentio stream."
Assert-Contains $player "Next episode" `
    "PlayerPage chrome must expose next-episode affordance."

Write-Host "Player P0 parity contract checks passed."

# The series page threads per-show loader identity into the playback context so the player's
# cinematic loader can show the right logo/still/episode line. Producer = TheatreSeries.qml;
# consumer = PlayerPage.playTorrent. No signal-signature or C++ change.
# docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$series = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) { if ($text -notlike "*$needle*") { throw $message } }

# --- producer: TheatreSeries captures the show logo and threads the loader keys ---
Assert-Contains $series 'property string logo'   "TheatreSeries must hold a normalized show logo."
Assert-Contains $series 'meta.logo'              "TheatreSeries must read meta.logo from the loaded metadata."
Assert-Contains $series 'function loadingEpisodeLine' "TheatreSeries must format the S/E/name loader line."

foreach ($key in @('"logo":','"episodeStill":','"loaderBackdrop":','"episodeLine":')) {
    Assert-Contains $series $key "The episode playback context must carry loader key $key"
}
# episodeStill must be normalized through the same art helper as the rest of the page.
Assert-Contains $series 'TheatreApi.normalizeArtUrl' "Loader art must be normalized via TheatreApi.normalizeArtUrl."

# --- consumer: PlayerPage.playTorrent copies all four values with fallbacks ---
Assert-Contains $player 'root.mediaLogo        = (playbackContext || ({})).logo'            "playTorrent must copy the show logo."
Assert-Contains $player '(playbackContext || ({})).episodeStill || (playbackContext || ({})).loaderBackdrop || posterUrl' "Loader art must fall back episodeStill -> loaderBackdrop -> poster."
Assert-Contains $player '(playbackContext || ({})).episodeLine || root.mediaSubtitle'       "Loader line must fall back to the media subtitle."

Write-Host "Player loader-context contract checks passed."

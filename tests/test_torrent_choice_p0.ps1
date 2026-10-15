# Torrent choice & continuity P0 contract (spec 2026-07-11).
# SHAPE ONLY: proves the wiring strings exist, not that they behave — behavior is
# covered by episode_browser_harness.qml (logic) and eyes-on (pixels).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function MustContain($file, $needle, $why) {
    $p = Join-Path $root $file
    if (!(Select-String -Path $p -Pattern ([regex]::Escape($needle)) -Quiet)) {
        throw "MISSING in ${file}: '$needle' ($why)"
    }
}

# SourcesSheet: download mode exists and the row action forks on it
MustContain "qml/SourcesSheet.qml" 'property string mode: "play"'      "sheet mode switch, play-default keeps every existing caller untouched"
MustContain "qml/SourcesSheet.qml" 'signal downloadRequested(var row)' "download-mode row action"
MustContain "qml/SourcesSheet.qml" 'sheet.mode === "download"'         "row click forks on mode"

# TheatreSeries: the ↓ opens the picker and the pick pins the request
MustContain "qml/TheatreSeries.qml" 'pendingDownloadEpisode'           "episode remembered while the sheet is open"
MustContain "qml/TheatreSeries.qml" 'onDownloadRequested'              "sheet pick lands back in the page"
MustContain "qml/TheatreSeries.qml" 'function queueEpisodeDownload(v, pick)' "pick rides the enqueue"

# DownloadStore: the pin survives into jobs() for the resolver
MustContain "native/player/downloadstore.cpp" '"infoHash"'             "jobs() whitelist exposes the pinned hash"
MustContain "native/player/downloadstore.cpp" '"fileIdx"'              "jobs() whitelist exposes the pinned file index"

# Play-mode per-row download (2026-07-19): the button beside the copy pins THIS torrent
MustContain "qml/SourcesSheet.qml" 'property bool titleQueued'          "one download per title - every row ticks once queued"
MustContain "qml/SourcesSheet.qml" 'refreshTitleQueued'                 "tick pre-set from the store when the sheet opens"
MustContain "qml/SourcesSheet.qml" 'id: dlBtn'                          "per-row download button beside the copy"
MustContain "qml/TheatreSeries.qml" 'sheetEpisode'                      "play-mode sheet remembers its episode for the per-row pick"
MustContain "qml/TheatreSeries.qml" 'function queueMovieDownload(pick)' "movie sheets honor the per-row pick too"

# Season checkout picker (2026-07-19): full-season torrents only, auto fallback
MustContain "qml/AddonClient.js" 'function isSeasonPack(row, season)'   "pure pack test - behavior covered by season_pack_harness.qml"
MustContain "qml/SourcesSheet.qml" 'signal seasonNoPacks()'             "zero packs hands the page the auto fallback"
MustContain "qml/SourcesSheet.qml" 'function baseRows()'                "season mode narrows the row universe to packs"
MustContain "qml/TheatreSeries.qml" 'function openSeasonPicker()'       "the season button fronts the picker, not a blind queue"
MustContain "qml/TheatreSeries.qml" 'onSeasonNoPacks'                   "no packs -> original per-episode auto path"
MustContain "qml/TheatreSeries.qml" '"fileIdx"] = -1'                   "season pick pins hash-only, file resolves per episode"
MustContain "qml/Main.qml" 'pin.fileIdx >= 0'                           "resolver forks exact pin vs hash-only pin"

# Main: pinned jobs skip the source search
MustContain "qml/Main.qml" 'pinnedPickFor'                             "resolver looks up the job's pin"

# Player: episode jumps prefer the playing torrent
MustContain "qml/PlayerPage.qml" 'EpisodeBrowser.js'                   "PlayerPage imports the pure lib"
MustContain "qml/PlayerPage.qml" 'pickContinuityRow'                   "jumpToEpisode routes through continuity"

Write-Host "Torrent choice & continuity P0 contract passed."

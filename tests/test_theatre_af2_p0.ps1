# AF2 Theatre detail — wiring contracts. Guards the episode machinery too.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) {
    if (-not $hay.Contains($needle)) { Write-Host "FAIL: $msg"; exit 1 }
}
$ts = Read-File "qml/TheatreSeries.qml"
Assert-Contains $ts 'TheatreFacts.factRows(meta' "resolve() assembles fact rows"
Assert-Contains $ts 'meta.cast || []' "resolve() captures cast names"
# The episode list is LAW — these needles must survive every AF2 task.
Assert-Contains $ts 'id: episodesSection' "episode section intact"
Assert-Contains $ts 'sources.show("series", page.episodeStreamId(ep.modelData)' "episode-row playback intact"
Assert-Contains $ts 'function heroEpisode()' "series Watch has a target"
Assert-Contains $ts 'page.episodeStreamId(ep)' "series Watch reuses the episode-row pipeline"
Assert-Contains $ts 'TheatreApi.loadAnimeCast' "cast fetch wired"
Assert-Contains $ts 'CastRow {' "cast row present"
Assert-Contains (Read-File "qml/CastRow.qml") 'initials(' "monogram fallback exists"
Assert-Contains (Read-File "qml/CastRow.qml") 'Theme { id: theme }' "CastRow declares its own Theme"
Assert-Contains $ts 'TheatreApi.moreLikeThis' "MLT fetch wired"
Assert-Contains $ts 'signal openItemRequested' "detail can open a sibling title"
Assert-Contains (Read-File "qml/Main.qml") 'openItemRequested.connect(win.openTheatreSeries)' "Main routes sibling opens"
Assert-Contains (Read-File "qml/MoreLikeThisRow.qml") 'Theme { id: theme }' "MoreLikeThisRow declares its own Theme"
Assert-Contains $ts 'kind: "play"' "Watch button uses the house play glyph, not a > character"
Write-Host "test_theatre_af2_p0 PASSED"

$ErrorActionPreference = "Stop"

# Contract for the v2 alternate-sources picker: the QML page drives the universal
# comics search facade (never Theatre's stream addons or the torrent engine), keeps
# an editable manual query, warns on weak matches, hosts the archive picker, and
# passes the CANONICAL edition title as the picker title. Behavioural coverage runs
# the headless page harness.

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    throw "qml.exe not found at $qmlExe"
}

# Frozen baseline gate (Tankorent Comic, 2026-07-15): the shipped comics-torrent C++
# harnesses must stay green through the whole collected-edition-in-pack build. Any later
# task that regresses one of these fails here, fast, before the QML contract checks.
$build = Join-Path $PSScriptRoot '..\native\build-msvc'
$baseline = @(
    'comic_torrent_query_planner_harness.exe',
    'comic_torrent_ranker_harness.exe',
    'comic_torrent_filepicker_harness.exe',
    'comic_torrents_search_harness.exe',
    'comic_downloader_ingest_harness.exe',
    'comic_torrent_pack_transport_harness.exe'
)
foreach ($name in $baseline) {
    $exe = Join-Path $build $name
    if (Test-Path -LiteralPath $exe) {
        & $exe | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "baseline harness $name failed: $LASTEXITCODE" }
    }
}

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (!$text.Contains($needle)) { throw $message }
}
function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { throw $message }
}

$page = Read-RepoFile "qml/ComicTorrentSourcesPage.qml"
$picker = Read-RepoFile "qml/ComicTorrentArchivePicker.qml"

# The page drives the universal-search facade - the same verbs ComicDownloader exposes.
Assert-Contains $page 'comicsApi.searchTorrentSources(' `
    "Auto search must call searchTorrentSources."
Assert-Contains $page 'comicsApi.searchTorrentSourcesQuery(' `
    "Manual query must call searchTorrentSourcesQuery."
Assert-Contains $page 'comicsApi.cancelTorrentSourceSearch(' `
    "The page must cancel its search on close/select."

# Row selection rides the AUTOMATIC pack-selection path (Task 10) - no manual
# file pick unless the transport itself says the manifest needs one.
Assert-Contains $page 'comicsApi.downloadTorrentEdition(' `
    "Row selection must call the automatic downloadTorrentEdition entry point."
Assert-Contains $page 'context.editionTitle, context.isbn, context.collects,' `
    "The canonical edition title/isbn/collects must be the match identity, not the release title."
Assert-Contains $page 'String(context.format || ""), row.infoHash' `
    "The catalog format must be threaded to the pack transport for format-scoped identity safety."
Assert-Contains $page 'comicsApi.chooseTorrentFiles(' `
    "An ambiguous manifest's manual pick must call chooseTorrentFiles."
Assert-Contains $page 'comicsApi.confirmCombinedArchive(' `
    "A combined-only manifest's explicit confirm must call confirmCombinedArchive."
Assert-Contains $page 'comicsApi.cancelDownload(' `
    "Backing out of a live acquisition must cancel it."

# The legacy single-archive verbs stay available as backend compat surface but
# this page no longer drives them directly (GetComics primary still does).
Assert-NotContains $page 'comicsApi.downloadTorrentSource(' `
    "Row selection must no longer call the legacy single-archive downloadTorrentSource."
Assert-NotContains $page 'comicsApi.chooseTorrentArchive(' `
    "The archive picker must no longer call the legacy single-archive chooseTorrentArchive."

# One typed-state driver renders every safe pack outcome.
Assert-Contains $page 'property string selectionState' `
    "The page must track exactly one selectionState string property."
Assert-Contains $page 'Inspecting pack' `
    "A chosen row enters an Inspecting pack state while metadata resolves."
Assert-Contains $page 'This pack is missing issues this edition needs.' `
    "An incomplete issue set must be named to the user, never silently downloaded."
Assert-Contains $page 'Download whole archive anyway' `
    "A combined-only manifest must require an explicit confirmation."
Assert-Contains $page 'FORMAT RANGE' `
    "Coverage rows must show a restrained FORMAT RANGE badge."
Assert-Contains $page 'TRUSTED' `
    "A trusted uploader must show a restrained trust marker."

# Manual query, weak confirmation, and the archive picker are all present.
Assert-Contains $page 'function submitManualQuery' `
    "An editable manual query must exist."
Assert-Contains $page 'This release does not closely match the collected edition.' `
    "Weak matches must warn before download."
Assert-Contains $page 'ComicTorrentArchivePicker' `
    "The page must host the archive picker for ambiguous packs."

# It stays comics-native: no Theatre stream-addon machinery, no direct engine.
Assert-NotContains $page 'AddonClient' `
    "The comics picker must not import Theatre's stream-addon client."
Assert-NotContains $page 'Torrentio' `
    "The comics picker must not reference Torrentio."
Assert-NotContains $page 'TorrentEngine' `
    "The comics picker must never touch the torrent engine directly."
Assert-NotContains $picker 'AddonClient' `
    "The archive picker must not import Theatre's stream-addon client."
Assert-NotContains $picker 'TorrentEngine' `
    "The archive picker must never touch the torrent engine directly."

# Only backend-validated candidates render; the picker emits an index to choose.
Assert-Contains $picker 'signal archiveChosen(int fileIndex)' `
    "The archive picker emits the chosen manifest index."

# The ledger offers the manual alternate action for idle, undownloaded editions and
# ComicSeriesPage hosts the picker - but the ledger NEVER auto-picks a torrent.
$ledger = Read-RepoFile "qml/ComicDbLedger.qml"
$series = Read-RepoFile "qml/ComicSeriesPage.qml"
Assert-Contains $ledger 'signal alternateSourcesRequested(var edition, string chId)' `
    "The ledger emits alternateSourcesRequested for the picker."
Assert-Contains $ledger 'Find alternate sources' `
    "The alternate action carries the 'Find alternate sources' label."
Assert-Contains $ledger 'ledger.alternateSourcesRequested(ed.modelData, ed.chId)' `
    "The alternate button emits only alternateSourcesRequested, never a direct download."
Assert-NotContains $ledger 'downloadIssueTorrent' `
    "The ledger must never auto-pick a torrent source."
Assert-Contains $series 'ComicTorrentSourcesPage' `
    "ComicSeriesPage hosts the full-screen alternate-sources picker."
Assert-Contains $series 'onAlternateSourcesRequested' `
    "ComicSeriesPage wires the ledger's alternate-sources signal to the picker."

# Behavioural: the headless page harness drives show -> weak-confirm -> archive-choice.
$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comic_torrent_sources_page_harness.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $output -notlike "*COMIC_TORRENT_SOURCES_PAGE_OK*") {
    throw "Comic torrent sources page harness failed (exit $LASTEXITCODE):`n$output"
}

Write-Host "comic torrent sources v2: OK"

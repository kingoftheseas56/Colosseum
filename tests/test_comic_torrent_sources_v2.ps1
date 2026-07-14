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

# The page drives the universal-search facade — the same verbs ComicDownloader exposes.
Assert-Contains $page 'comicsApi.searchTorrentSources(' `
    "Auto search must call searchTorrentSources."
Assert-Contains $page 'comicsApi.searchTorrentSourcesQuery(' `
    "Manual query must call searchTorrentSourcesQuery."
Assert-Contains $page 'comicsApi.cancelTorrentSourceSearch(' `
    "The page must cancel its search on close/select."
Assert-Contains $page 'comicsApi.downloadTorrentSource(' `
    "Row selection must call downloadTorrentSource."
Assert-Contains $page 'comicsApi.chooseTorrentArchive(' `
    "Archive choice must call chooseTorrentArchive."

# Manual query, weak confirmation, and the archive picker are all present.
Assert-Contains $page 'function submitManualQuery' `
    "An editable manual query must exist."
Assert-Contains $page 'This release does not closely match the collected edition.' `
    "Weak matches must warn before download."
Assert-Contains $page 'ComicTorrentArchivePicker' `
    "The page must host the archive picker for ambiguous packs."
Assert-Contains $page 'context.editionTitle, pendingRow.infoHash' `
    "The canonical edition title must be the picker title, not the release title."

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

# Behavioural: the headless page harness drives show -> weak-confirm -> archive-choice.
$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comic_torrent_sources_page_harness.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $output -notlike "*COMIC_TORRENT_SOURCES_PAGE_OK*") {
    throw "Comic torrent sources page harness failed (exit $LASTEXITCODE):`n$output"
}

Write-Host "comic torrent sources v2: OK"

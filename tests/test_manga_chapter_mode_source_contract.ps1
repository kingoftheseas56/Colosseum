$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$engine = Get-Content (Join-Path $root "native/MangaEngine.h") -Raw
$series = Get-Content (Join-Path $root "qml/MangaSeries.qml") -Raw
$room = Get-Content (Join-Path $root "qml/MangaReadingRoom.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
function Need($text, $needle, $message) { if (!$text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 } }
function Absent($text, $needle, $message) { if ($text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 } }
Need $engine "chapterCatalogue(" "MangaEngine must expose correlated chapter catalogue request"
Need $engine "chapterCatalogueResults" "MangaEngine must identify chapter catalogue results"
Need $engine "chapterCatalogueFailed" "MangaEngine must identify chapter catalogue failures"
Need $series "MangaChapterSeriesView" "MangaSeries must host restored chapter surface"
Need $series "property bool chapterMode" "MangaSeries must own the sibling mode switch"
Need $series "onChapterModeRequested" "Tankoban surface must be able to enter Chapter mode"
Need $series "entryKind: page.openEntryKind" "shared reader contract must remain intact"
Need $room "signal chapterModeRequested()" "current Tankoban surface must expose Chapter-mode switch"
Absent $main "TankobanChapterMigration::run" "boot must no longer purge restored chapter data"
Write-Host "manga chapter mode source contract: OK"

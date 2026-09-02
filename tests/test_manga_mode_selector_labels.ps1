$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$room = Get-Content (Join-Path $root "qml/MangaReadingRoom.qml") -Raw
$chapters = Get-Content (Join-Path $root "qml/MangaChapterSeriesView.qml") -Raw
$shared = Get-Content (Join-Path $root "qml/MangaSeriesSharedHeader.qml") -Raw
function Need($text, $needle, $message) { if (!$text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 } }
function Absent($text, $needle, $message) { if ($text.Contains($needle)) { Write-Host "FAIL: $message"; exit 1 } }
Need $room 'MangaSeriesSharedHeader' "Tankoban surface must use shared manga header"
Need $chapters 'MangaSeriesSharedHeader' "Chapter surface must use shared manga header"
Need $shared 'text: "Tankoban Mode"' "shared selector must name Tankoban Mode explicitly"
Need $shared 'text: "Chapter Mode"' "shared selector must name Chapter Mode explicitly"
Absent $shared 'text: "Off"' "shared selector must not use Off"
Absent $shared 'text: "On"' "shared selector must not use On"
Absent $room 'objectName: "tankobanSeriesPrimaryAction"' "Search Nyaa must leave the Tankoban hero action cluster"
Need $shared 'LibraryButton' "shared header must retain the Library action"
Absent $room 'LibraryButton' "Tankoban surface must not duplicate Library outside shared header"
Absent $chapters 'LibraryButton' "Chapter surface must not duplicate Library outside shared header"
Write-Host "manga mode selector labels: OK"
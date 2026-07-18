$ErrorActionPreference = "Stop"

# One Piece bespoke universe contract (Hemanth, 2026-07-18): the retired Captain's Chart
# stays gone. The replacement is a minimal-prose Road Poneglyph chamber whose four stones
# route to the pinned Watch, Read, Films, and Adaptations rooms.

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

$page = Read-File "qml/OnePieceUniversePage.qml"
foreach ($n in @('property var roomLabels', 'function roomCount(', 'function scrollToRoom(',
                 'component RoadStone', 'component MediaPortal', 'component MangaGate',
                 'component FilmFrame', 'WATCH', 'READ', 'FILMS', 'ADAPTATIONS',
                 '#8E1826', '#FFB35B', 'activeFocusOnTab: true',
                 'Keys.onReturnPressed', 'maximumLineCount: 2', 'property bool reached',
                 '1100+ EPISODES', 'status === Image.Ready',
                 'root.width >= 760 ? 1060 : root.width >= 520 ? 1580 : 2440')) {
    Assert-Contains $page $n "One Piece chamber must carry: $n"
}
foreach ($n in @("THE CAPTAIN'S CHART", 'THE LOG POSE', "THE SHIP'S LOG",
                 'THE BOUNTY BOARD', 'SagaIsland', 'WantedPoster', 'nodeX(', 'nodeY(')) {
    Assert-Lacks $page $n "Retired One Piece lineage returned: $n"
}

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'OnePieceUniversePage.qml' "Main must route One Piece to its bespoke page."

Write-Host "one piece universe p0: OK"

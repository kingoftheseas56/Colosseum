$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$shield = Join-Path $root 'qml/FullscreenTransitionShield.qml'

if (-not (Test-Path $shield)) {
    throw 'FullscreenTransitionShield.qml is missing'
}

$shieldText = Get-Content $shield -Raw
if ($main -notmatch 'FullscreenTransitionShield\s*\{') {
    throw 'Main does not own the fullscreen shield'
}
if ($main -notmatch 'function toggleFullscreenShell\(\)[\s\S]{0,250}fullscreenTransition\.begin\(\)') {
    throw 'all fullscreen requests must enter through the shield'
}
if ($main -notmatch 'onActivated:\s*win\.toggleFullscreenShell\(\)') {
    throw 'F11 bypasses the shared transition path'
}
if ($shieldText -notmatch 'interval:\s*250') {
    throw 'shield lost its bounded frame fallback'
}
if ($shieldText -notmatch 'function onFrameSwapped\(\)') {
    throw 'shield does not wait for a presented frame'
}

$player = Get-Content (Join-Path $root 'qml/PlayerPage.qml') -Raw
if ($player -notmatch 'signal fullscreenRequested\(\)') {
    throw 'PlayerPage lost fullscreenRequested()'
}
# (Restore glyph moved: PlayerPage's hand-drawn Canvas path became a Lucide icon in
#  PlayerIcon.qml with A4's 45c3955 — assert the mapping there, not the old Canvas kind.)
$playerIcon = Get-Content (Join-Path $root 'qml/PlayerIcon.qml') -Raw
if ($playerIcon -notmatch 'case "fullscreenExit":') {
    throw 'PlayerIcon has no fullscreenExit mapping (restore glyph lost)'
}
if ($player -notmatch 'icon:\s*root\.shellWindowed\s*\?\s*"fullscreen"\s*:\s*"fullscreenExit"') {
    throw 'player titlebar does not show the available fullscreen action'
}
if ($player -notmatch 'onClicked:\s*root\.fullscreenRequested\(\)') {
    throw 'player fullscreen button does not emit the semantic request'
}
if ($main -notmatch 'item\.fullscreenRequested\.connect\(win\.toggleFullscreenShell\)') {
    throw 'playerLayer does not route fullscreen through Main'
}

$bookFullscreen = Join-Path $root 'assets/icons/reader2/fullscreen.svg'
$bookFullscreenExit = Join-Path $root 'assets/icons/reader2/fullscreen-exit.svg'
if (-not (Test-Path $bookFullscreen) -or -not (Test-Path $bookFullscreenExit)) {
    throw 'book reader fullscreen SVG pair is incomplete'
}
$bookTop = Get-Content (Join-Path $root 'qml/reader2/TopBar.qml') -Raw
$bookChrome = Get-Content (Join-Path $root 'qml/reader2/ReaderChrome.qml') -Raw
$bookShell = Get-Content (Join-Path $root 'qml/reader2/ReaderShell.qml') -Raw
foreach ($entry in @(
    @{ Text = $bookTop; Name = 'TopBar' },
    @{ Text = $bookChrome; Name = 'ReaderChrome' },
    @{ Text = $bookShell; Name = 'ReaderShell' }
)) {
    if ($entry.Text -notmatch 'signal fullscreenRequested\(\)') {
        throw "$($entry.Name) lost fullscreenRequested()"
    }
}
if ($bookTop -notmatch 'root\.shellWindowed[\s\S]{0,180}reader2/fullscreen\.svg[\s\S]{0,180}reader2/fullscreen-exit\.svg') {
    throw 'book TopBar does not swap the reader-specific fullscreen action icon'
}
if ($bookChrome -notmatch 'onFullscreenRequested:\s*chrome\.fullscreenRequested\(\)') {
    throw 'ReaderChrome does not forward the TopBar fullscreen request'
}
if ($bookShell -notmatch 'onFullscreenRequested:\s*shell\.fullscreenRequested\(\)') {
    throw 'ReaderShell does not forward the chrome fullscreen request'
}
if ($main -notmatch 'item\.minimized\.connect\(win\.minimizeBookReader\)[\s\S]{0,160}item\.fullscreenRequested\.connect\(win\.toggleFullscreenShell\)') {
    throw 'bookReaderLayer does not route fullscreen through Main'
}

$mangaReader = Get-Content (Join-Path $root 'qml/MangaReader.qml') -Raw
if ($mangaReader -notmatch 'import\s+"comicreader"') {
    throw 'MangaReader no longer delegates to the canonical comic reader module'
}
if ($mangaReader -notmatch 'ComicReaderShell\s*\{[\s\S]{0,240}objectName:\s*"comicReaderShell"') {
    throw 'MangaReader does not expose the canonical ComicReaderShell identity'
}
$readerShell = Get-Content (Join-Path $root 'qml/comicreader/ComicReaderShell.qml') -Raw
if ($readerShell -notmatch 'signal fullscreenRequested\(\)') {
    throw 'ComicReaderShell lost fullscreenRequested()'
}
if ($readerShell -notmatch 'onToggleFullscreen:\s*reader\.fullscreenRequested\(\)') {
    throw 'ComicReaderShell does not forward the input fullscreen request'
}
if ($readerShell -notmatch 'onFullscreenRequested:\s*reader\.fullscreenRequested\(\)') {
    throw 'ComicReaderShell does not forward the HUD fullscreen request'
}
$readerHud = Get-Content (Join-Path $root 'qml/comicreader/ComicReaderHud.qml') -Raw
if ($readerHud -notmatch 'signal fullscreenRequested\(\)') {
    throw 'ComicReaderHud lost fullscreenRequested()'
}
if ($readerHud -notmatch 'objectName:\s*"hudFullscreenButton"') {
    throw 'ComicReaderHud lost its fullscreen button identity'
}
if ($readerHud -notmatch 'onTapped:\s*hud\.fullscreenRequested\(\)') {
    throw 'ComicReaderHud fullscreen button does not emit the semantic request'
}
foreach ($hostName in @('MangaSeries.qml', 'ComicSeries.qml', 'ComicSeriesPage.qml')) {
    $hostText = Get-Content (Join-Path $root "qml/$hostName") -Raw
    if ($hostText -notmatch 'signal readerFullscreenRequested\(\)') {
        throw "$hostName lost readerFullscreenRequested()"
    }
    if ($hostText -notmatch 'onFullscreenRequested:\s*page\.readerFullscreenRequested\(\)') {
        throw "$hostName does not forward MangaReader fullscreen"
    }
}
$comicMainLinks = [regex]::Matches(
    $main, 'item\.readerFullscreenRequested\.connect\(win\.toggleFullscreenShell\)').Count
if ($comicMainLinks -ne 3) {
    throw "Main must connect all three comic-reader hosts (found $comicMainLinks)"
}

# World pages share ONE TopBar with home; the world shell must forward its fullscreen
# click and Main must connect it (the 2026-07-19 works-once bug: WorldPage swallowed the
# signal, so the icon was dead on every world page while home worked).
$worldPage = Get-Content (Join-Path $root 'qml/WorldPage.qml') -Raw
if ($worldPage -notmatch 'signal fullscreenClicked\(\)') {
    throw 'WorldPage lost fullscreenClicked()'
}
if ($worldPage -notmatch 'onFullscreenClicked:\s*world\.fullscreenClicked\(\)') {
    throw 'WorldPage does not forward the TopBar fullscreen click'
}
if ($main -notmatch 'item\.fullscreenClicked\.connect\(win\.toggleFullscreenShell\)') {
    throw 'Main does not route the world-page fullscreen click through the shell toggle'
}
if ($main -notmatch 'onFullscreenClicked:\s*win\.toggleFullscreenShell\(\)') {
    throw 'home TopBar fullscreen click does not route through the shell toggle'
}

Write-Host 'test_fullscreen_controls_p0: all fullscreen control contracts PASS'

param([string]$RootOverride = "")
$ErrorActionPreference = "Stop"

$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
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

$main = Read-File "qml/Main.qml"
$taskbar = Read-File "qml/Taskbar.qml"
$policy = Read-File "qml/ShellBackPolicy.js"
Assert-Contains $main 'import "ShellBackPolicy.js" as ShellBackPolicy' `
    "Main.qml must import the pure shell Escape policy."
Assert-Contains $main 'function handleEscape()' `
    "Main.qml must expose one shell Escape dispatcher."
Assert-Contains $main 'ShellBackPolicy.actionFor(win.shellEscapeState())' `
    "The shell dispatcher must select through ShellBackPolicy."
Assert-Contains $main 'stremioSyncOpen: stremioPanel.shown,' `
    "The Stremio Main Sync overlay must participate in shell Escape arbitration."
Assert-Contains $policy 'if (on(s.stremioSyncOpen)) return "stremioSync"' `
    "The Stremio Main Sync overlay must win Escape above the underlying page."
Assert-Contains $main 'case "stremioSync": win.closeStremioSyncPanel(); return' `
    "Escape must close Main Sync through its existing close route."
Assert-Contains $main 'semanticId: "global.escape"' `
    "The semantic keyboard registry must own the one global Escape command."
Assert-Contains $main 'sequences: ["Escape"]' `
    "The global Escape command must retain its physical Escape sequence."
Assert-Contains $main 'onTriggered: win.handleEscape()' `
    "The global Escape command must do nothing except enter the dispatcher."
Assert-Lacks $main 'Shortcut { sequences: ["Escape"]; onActivated: {' `
    "The old inline Escape decision chain must be gone."
Assert-Lacks $main 'else if (bookReaderLayer.active) win.closeBookReader()' `
    "The old raw book-reader Escape branch must be gone."
Assert-Contains $main 'function requestPlayerEscape()' `
    "Main.qml must delegate player Escape through the player surface contract."
Assert-Contains $main 'function requestBookReaderEscape()' `
    "Main.qml must delegate book Escape through ReaderShell."
Assert-Contains $main 'function requestComicReaderEscape()' `
    "Main.qml must delegate comic Escape through ComicReaderShell.closeTop()."
Assert-Contains $main '|| vaultComicLayer.active' `
    "Standalone Vault comics must participate in immersiveSurfaceOpen."
Assert-Contains $main 'item.backRequested.connect(win.closeVaultPage)' `
    "VaultPage terminal Back must retain the existing shell exit seam."
Assert-Contains $main 'vaultLayer.item.handleBack()' `
    "Shell Escape must delegate into VaultPage instead of blindly deactivating it."
Assert-Contains $main 'ratingsReviewsActive: ratingsReviewsLayer.active,' `
    "Ratings Reviews must participate in the one shell Escape state snapshot."
Assert-Contains $main 'case "ratingsReviews":' `
    "Shell Escape must expose one Ratings Reviews arbitration branch."
Assert-Contains $main 'ratingsReviewsLayer.item.handleBack()' `
    "The Ratings Reviews branch must delegate to the host Back contract."
Assert-Contains $main 'syncCenterActive: syncCenterLayer.active,' `
    "Sync Center must participate in the one shell Escape state snapshot."
Assert-Contains $main 'case "syncCenter":' `
    "Shell Escape must close the Sync dossier before the full page."
Assert-Contains $main 'syncCenterLayer.item.requestEscape()' `
    "Sync Center Escape must delegate to its nested-dossier Back contract."
Assert-Contains $main 'function openSyncCenterPage()' `
    "The taskbar Sync intent must enter the real full-page route."
Assert-Contains $main 'function closeSyncCenterPage()' `
    "The Sync Center must expose one shell close route."
Assert-Contains $main 'win.focusTrackersDoor()' `
    "Closing the Sync Center must restore keyboard focus to the top-bar trackers door."
Assert-Contains $main 'function focusTrackersDoor()' `
    "The shell must hand focus back to the trackers door of the front TopBar."
Assert-Contains $main 'source: "TrackerSyncCenterPage.qml"' `
    "The Sync Center route must load the production page, not a test surface."
Assert-Contains $main 'item.mainSyncRequested.connect(win.openStremioSyncPanel)' `
    "The distinct Main Sync row must open the existing Stremio panel."
Assert-Contains $main 'onTrackersClicked: win.toggleSyncCenterPage()' `
    "The home trackers door must toggle only the Sync Center route."
Assert-Contains $main 'item.trackersClicked.connect(win.toggleSyncCenterPage)' `
    "Every world's trackers door must toggle only the Sync Center route."
if ([regex]::Matches($main, [regex]::Escape('ratingsReviewsLayer.item.handleBack()')).Count -ne 1) {
    throw "Shell Escape must call the Ratings Reviews host exactly once per branch."
}

$vault = Read-File "qml/VaultPage.qml"
Assert-Contains $vault 'function handleBack()' `
    "VaultPage must arbitrate its own internal overlays before leaving Vault."
Assert-Contains $vault 'if (root.detailSheetVisible)' `
    "Vault Back must close the detail sheet before leaving."
Assert-Contains $vault 'if (root.folderDetailOpen)' `
    "Vault Back must close folder detail before leaving."
Assert-Contains $vault 'if (root.cardVisible)' `
    "Vault Back must dismiss the founding card before leaving Vault."
Assert-Contains $vault 'if (identifyDialog.opened)' `
    "Vault Back must dismiss Identify before touching browse state."
Assert-Contains $vault 'if (root.hiddenViewActive || root.crumbStack.length > 1)' `
    "Vault Back must ascend browse state before exiting Vault."

$identity = Read-File "qml/VaultIdentityCeremonyDialog.qml"
Assert-Contains $identity 'closePolicy: Popup.NoAutoClose' `
    "Identity ceremony Escape must not auto-close behind the owner's state cleanup."
Assert-Contains $identity 'signal cancelRequested()' `
    "Identity ceremony must expose an explicit cancel seam."
Assert-Contains $main 'onCancelRequested: win.cancelPendingIdentityCeremony()' `
    "The shell must own ceremony cancellation so pending route state is cleared with the popup."

$player1 = Read-File "qml/PlayerPage.qml"
Assert-Contains $player1 'function requestEscape()' `
    "Player 1 must expose its menu-first Escape semantics."
Assert-Contains $player1 'case "escape": root.requestEscape(); return' `
    "Player 1 local Escape and shell Escape must converge on requestEscape()."

$player2Host = Read-File "qml/player2host/Player2Page.qml"
Assert-Contains $player2Host 'function requestEscape() { shell.requestEscape() }' `
    "Player 2 host must mirror Player 1's requestEscape interface."
$player2 = Read-File "qml/player2/Player2Shell.qml"
Assert-Contains $player2 'function requestEscape()' `
    "Player 2 shell must expose menu-first Escape semantics."
Assert-Contains $player2 'case Qt.Key_Escape:' `
    "Player 2 must retain a local Escape route."
Assert-Contains $player2 'shell.requestEscape()' `
    "Player 2 local Escape must converge on requestEscape()."

$reader2 = Read-File "qml/reader2/ReaderShell.qml"
Assert-Contains $reader2 'function requestEscape()' `
    "Reader 2 must expose its overlay-first Escape cascade."
Assert-Contains $reader2 'else shell.goBack()' `
    "Reader 2 Escape must flush and close only after its overlays are gone."

foreach ($f in @("qml/MangaSeries.qml", "qml/ComicSeries.qml", "qml/ComicSeriesPage.qml")) {
    $text = Read-File $f
    Assert-Contains $text 'function requestReaderEscape()' `
        "$f must expose the embedded ComicReaderShell Escape seam."
    Assert-Contains $text 'readerLayer.closeTop()' `
        "$f Escape must preserve ComicReaderShell.closeTop(), not leave the reader."
}

$vaultComic = Read-File "qml/comicreader/VaultComicReader.qml"
Assert-Contains $vaultComic 'function requestEscape() { shell.closeTop() }' `
    "Standalone Vault comics must preserve ComicReaderShell.closeTop() Escape semantics."

Write-Host "test_shell_back_arbitration_p0: PASS"

$cmake = Read-File "tests/CMakeLists.txt"
foreach ($name in @('shell_back_arbitration_p0', 'taskbar_immersive_readers_p0',
                     'back_action_p0', 'taskbar_download_reveal_p0')) {
    Assert-Contains $cmake $name "CTest must register Function 0003 gate $name."
}

Write-Host "test_shell_back_arbitration_p0: CTest registrations PASS"

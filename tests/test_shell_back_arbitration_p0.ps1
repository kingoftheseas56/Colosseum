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
Assert-Contains $main 'import "ShellBackPolicy.js" as ShellBackPolicy' `
    "Main.qml must import the pure shell Escape policy."
Assert-Contains $main 'function handleEscape()' `
    "Main.qml must expose one shell Escape dispatcher."
Assert-Contains $main 'ShellBackPolicy.actionFor(win.shellEscapeState())' `
    "The shell dispatcher must select through ShellBackPolicy."
Assert-Contains $main 'onActivated: win.handleEscape()' `
    "The global Escape Shortcut must do nothing except enter the dispatcher."
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

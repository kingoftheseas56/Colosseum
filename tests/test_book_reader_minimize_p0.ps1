# Contract: the fresh book reader (reader2) can MINIMIZE to a taskbar tile, comic-reader
# parity (Hemanth 2026-07-18: "books should minimize too" — the Task 16 swap had dropped
# the affordance). The chain guarded here, link by link:
#   TopBar minimize icon -> ReaderChrome.minimizeRequested -> ReaderShell.goMinimize()
#   (flush BEFORE emit, same discipline as goBack) -> minimized() -> Main.qml
#   minimizeBookReader -> Sessions.switchTo("") parks the session.
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$main   = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$shell  = Get-Content (Join-Path $root 'qml/reader2/ReaderShell.qml') -Raw
$chrome = Get-Content (Join-Path $root 'qml/reader2/ReaderChrome.qml') -Raw
$topbar = Get-Content (Join-Path $root 'qml/reader2/TopBar.qml') -Raw

# TopBar: the icon exists and fires the signal.
if ($topbar -notmatch 'signal minimizeRequested\(\)') { throw 'TopBar lost signal minimizeRequested()' }
if ($topbar -notmatch 'reader2/minimize\.svg') { throw 'TopBar has no minimize icon — the verb has no button' }
if (-not (Test-Path (Join-Path $root 'assets/icons/reader2/minimize.svg'))) {
    throw 'assets/icons/reader2/minimize.svg missing — the minimize button renders blank'
}

# ReaderChrome: pass-through.
if ($chrome -notmatch 'signal minimizeRequested\(\)') { throw 'ReaderChrome lost signal minimizeRequested()' }
if ($chrome -notmatch 'onMinimizeRequested:\s*chrome\.minimizeRequested\(\)') {
    throw 'ReaderChrome no longer forwards the TopBar minimize'
}

# ReaderShell: flush-first verb + the minimized() signal the embedder listens on.
if ($shell -notmatch 'signal minimized\(\)') { throw 'ReaderShell lost signal minimized()' }
if ($shell -notmatch 'function goMinimize\(\)\s*\{\s*shell\.flushProgressSave\(\);\s*shell\.minimized\(\)') {
    throw 'goMinimize must flush the pending position save BEFORE emitting minimized() (a turn inside the debounce window would be lost)'
}
if ($shell -notmatch 'onMinimizeRequested:\s*shell\.goMinimize\(\)') {
    throw 'ReaderShell no longer routes the chrome minimize to goMinimize'
}

# Main.qml: the embedder connects it and the verb parks the session.
if ($main -notmatch 'item\.minimized\.connect\(win\.minimizeBookReader\)') {
    throw 'bookReaderLayer no longer connects minimized() -> minimizeBookReader'
}
$verb = [regex]::Match($main, 'function minimizeBookReader\(\)[\s\S]*?\n    \}').Value
if (-not $verb) { throw 'minimizeBookReader is gone from Main.qml' }
if ($verb -notmatch 'Sessions\.switchTo\(""\)') {
    throw 'minimizeBookReader must park via Sessions.switchTo("") — that is what keeps the taskbar tile'
}

Write-Host 'test_book_reader_minimize_p0: PASS (icon -> chrome -> shell flush-first -> session park, all wired)'

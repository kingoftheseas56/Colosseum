# Arc 54 Update web-port contract.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$topbar = Get-Content (Join-Path $root 'qml/TopBar.qml') -Raw
$bar = Get-Content (Join-Path $root 'qml/Taskbar.qml') -Raw
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$feed = Get-Content (Join-Path $root 'native/webui/feeds/UpdateFeed.cpp') -Raw
$surface = Get-Content (Join-Path $root 'web/developer-colosseum/surfaces/update/surface.js') -Raw
$surfaceCss = Get-Content (Join-Path $root 'web/developer-colosseum/surfaces/update/surface.css') -Raw
$shell = Get-Content (Join-Path $root 'web/developer-colosseum/core/shell.js') -Raw
$shellCss = Get-Content (Join-Path $root 'web/developer-colosseum/styles/shell.css') -Raw
$index = Get-Content (Join-Path $root 'web/developer-colosseum/index.html') -Raw

if (Test-Path (Join-Path $root 'qml/UpdatePage.qml')) { throw 'retired UpdatePage.qml still exists' }
if (Test-Path (Join-Path $root 'qml/update/UpdateLivingGallery.qml')) { throw 'retired UpdateLivingGallery.qml still exists' }

if ($topbar -notmatch 'objectName:\s*"colosseumTopbarUpdateButton"') { throw 'TopBar Update glyph missing' }
if ($topbar -notmatch 'objectName:\s*"colosseumTopbarUpdateBadge"') { throw 'TopBar Update badge missing' }
if ($bar -match 'colosseumUpdateTaskbarButton|signal updateClicked|property bool updateActive') {
    throw 'Taskbar retains retired Update plumbing'
}

if ($main -match 'updateLayer|UpdatePage\.qml|closeUpdatePage') { throw 'Main.qml retains native Update fallback' }
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?openWebPage\("update"\)') {
    throw 'Main.qml does not route Update to page.update'
}
if ($main -notmatch 'onUpdateClicked:\s*win\.openUpdatePage\(\)') { throw 'TopBar Update glyph is not web-routed' }
if ($main -notmatch 'updateAvailable:\s*typeof Updates' -or $main -notmatch 'updateUnseen:\s*typeof Updates') {
    throw 'TopBar update state is not bound to UpdateService'
}

if ($index -notmatch 'surfaces/update/surface\.css' -or $index -notmatch 'surfaces/update/surface\.js') {
    throw 'Update web surface is not linked'
}
if ($index -notmatch 'id="update-button"' -or $index -notmatch 'id="update-badge"') {
    throw 'Web TopBar has no visible Update door/badge'
}
if ($shell -notmatch "update-button'.*?env\.door\('update'\)" -or
    $shell -notmatch "port\.subscribe\('page\.update'") {
    throw 'Web TopBar Update door is not routed/status-bound to page.update'
}
if ($shell -notmatch "search-button'\)\.hidden\s*=\s*route\.name\s*!==\s*'world'" -or
    $shell -notmatch "update-button'\)\.hidden\s*=\s*route\.name\s*!==\s*'home'") {
    throw 'Search/Update do not swap in the Home/world TopBar slot'
}
if ($surface -notmatch "CW\.router\.register\('page\.update'") { throw 'page.update surface is not registered' }
if ($surface -notmatch "env\.port\.subscribe\('page\.update'") { throw 'page.update does not subscribe to its native feed' }
if ($surface -notmatch "'data-focus': true" -or $surface -notmatch "'data-key': 'update\.primary'") {
    throw 'Update web controls do not use the shared focus engine contract'
}
if ($surface -match 'keydown|fetch\(') { throw 'Update surface bypasses the shared focus/data contract' }

foreach ($action in @('page.update.seen','page.update.check','page.update.download','page.update.pause','page.update.install')) {
    if ($feed -notmatch [regex]::Escape($action)) { throw "UpdateFeed missing action $action" }
}
if ($feed -notmatch 'entry\.capture\s*=\s*captureUpdate' -or $feed -notmatch 'ownerSignals\.append') {
    throw 'Update feed is not bound to GUI-thread capture + UpdateService change signals'
}
if ($feed -match 'page\.update\.wait') { throw 'Update feed retains retired long-poll action' }

if ($shell -notmatch "page\.update" -or $shellCss -notmatch 'data-surface="page\.update"') {
    throw 'Update full-bleed shell mode is not wired'
}
if ($surfaceCss -notmatch 'height:100vh') { throw 'Update stage no longer owns the full viewport' }

Write-Host 'test_update_taskbar_p0: PASS (web-owned Update route, feed/actions, full-bleed shell, no QML fallback)'

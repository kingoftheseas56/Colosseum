# Update taskbar notification P0: static contract for the permanent shell affordance.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bar = Get-Content (Join-Path $root 'qml/Taskbar.qml') -Raw
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$iconPath = Join-Path $root 'assets/icons/update.svg'

if (!(Test-Path $iconPath)) { throw 'missing update.svg' }
$svg = Get-Content $iconPath -Raw
if ($svg -match 'currentColor') { throw 'update.svg uses currentColor' }
if ($svg -notmatch 'stroke\s*=\s*"#[0-9a-fA-F]{6}"|fill\s*=\s*"#[0-9a-fA-F]{6}"') {
    throw 'update.svg has no explicit concrete stroke/fill'
}

foreach ($contract in @('signal updateClicked', 'property bool updateActive',
                        'property bool updateAvailable', 'property bool updateUnseen')) {
    if ($bar -notmatch [regex]::Escape($contract)) { throw "Taskbar missing $contract" }
}
if ($bar -notmatch 'objectName:\s*"colosseumUpdateTaskbarButton"') {
    throw 'update taskbar button has no stable objectName'
}
if ($bar -notmatch 'objectName:\s*"colosseumUpdateBadge"') {
    throw 'update badge has no stable objectName'
}
if ($bar -notmatch 'visible:\s*bar\.open') { throw 'update item is not gated by expanded taskbar visibility' }
if ($bar -notmatch 'visible:\s*bar\.updateAvailable') { throw 'update availability badge is not persistent' }
if ($bar -notmatch 'SequentialAnimation[\s\S]*?running:\s*bar\.updateUnseen') {
    throw 'update pulse is not driven only by unseen state'
}
if ($bar -notmatch 'visible:\s*bar\.updateActive[\s\S]*?height:\s*3') {
    throw 'update active state has no standard gold underline'
}
if ($bar -notmatch 'Accessible\.name:[\s\S]*Update available') {
    throw 'update control has no accessible Update available name'
}
if ($main -notmatch 'updateAvailable:\s*typeof Updates' -or
    $main -notmatch 'updateUnseen:\s*typeof Updates' -or
    $main -notmatch 'updateActive:\s*updateLayer\.active') {
    throw 'Main.qml does not bind the live Updates service to Taskbar'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?updateLayer\.active\s*=\s*true') {
    throw 'Main.qml has no openUpdatePage route'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?taskbar\.open\s*=\s*true[\s\S]*?taskbar\.autoRevealed\s*=\s*false') {
    throw 'opening Update does not pin an auto-revealed taskbar open for the chronicle'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?Updates\.markSeen\(\)') {
    throw 'opening Update does not mark the release seen'
}
if ($main -notmatch 'id:\s*updateLayer') { throw 'Main.qml has no updateLayer loader' }
$immersiveBlock = [regex]::Match($main, 'readonly property bool immersiveSurfaceOpen:[^\r\n]*(?:\r?\n\s*\|\|[^\r\n]*)*').Value
if ($immersiveBlock -match [regex]::Escape('updateLayer.active')) {
    throw 'UpdatePage still suppresses the ordinary taskbar while the chronicle is open'
}
if ($main -notmatch 'visible:\s*!win\.immersiveSurfaceOpen') {
    throw 'Taskbar is not visible while the non-immersive Update page is active'
}
foreach ($contract in @('property var updatePresentation', 'signal updatePrimaryActionRequested',
                        'objectName: "colosseumUpdateStatusText"',
                        'objectName: "colosseumUpdateStatusMetadata"',
                        'objectName: "colosseumUpdateProgressTrack"',
                        'objectName: "colosseumUpdatePrimaryAction"')) {
    if ($bar -notmatch [regex]::Escape($contract)) { throw "Taskbar is missing taskbar-owned updater contract: $contract" }
}
if ($bar -match 'UpdateStatusRail') { throw 'Taskbar retains the rejected secondary update rail' }
$page = Get-Content (Join-Path $root 'qml/UpdatePage.qml') -Raw
if ($page -match 'UpdateStatusRail|colosseumUpdateStatusRail') {
    throw 'UpdatePage retains the rejected secondary status surface'
}
if (Test-Path (Join-Path $root 'qml/update/UpdateStatusRail.qml')) {
    throw 'rejected UpdateStatusRail.qml component still exists'
}
if ($page -notmatch 'taskbarPresentation') {
    throw 'UpdatePage does not expose the single updater presentation mapping to Taskbar'
}
$gallery = Get-Content (Join-Path $root 'qml/update/UpdateLivingGallery.qml') -Raw
foreach ($contract in @('text: "COLOSSEUM UPDATE"', 'activeFocusOnTab: true',
                        'Keys.onLeftPressed:', 'Keys.onRightPressed:',
                        'Keys.onReturnPressed:', 'Keys.onSpacePressed:')) {
    if ($gallery -notmatch [regex]::Escape($contract)) {
        throw "Living gallery is missing keyboard/focus contract: $contract"
    }
}
if ($gallery -match 'persistent status rail') {
    throw 'Living gallery retains stale rejected-status-rail terminology'
}
if ($gallery -notmatch 'width:\s*44' -or $gallery -notmatch 'height:\s*44') {
    throw 'chapter navigator does not expose 44px logical hit targets'
}
if ($bar -notmatch 'objectName:\s*"colosseumUpdatePrimaryAction"[\s\S]{0,1800}?Layout\.preferredHeight:\s*44') {
    throw 'taskbar update primary action does not expose a 44px logical hit target'
}
if ($bar -match 'Math\.round\(bar\.updateProgress \* 100\) \+ "%"') {
    throw 'taskbar still renders a duplicate standalone progress percentage'
}
foreach ($contract in @('readonly property real automationStageOpacity',
                        'readonly property bool automationStageSettled')) {
    if ($gallery -notmatch [regex]::Escape($contract)) {
        throw "gallery visual-readiness contract is missing named settled-stage automation: $contract"
    }
}
if ($gallery -notmatch 'readonly property bool visualContentReady:[\s\S]*?automationStageSettled') {
    throw 'gallery visual-readiness contract does not require the named stage-settled condition'
}

Write-Host 'test_update_taskbar_p0: PASS (icon, badge, persistent taskbar, safe action surface, and shell route contract)'

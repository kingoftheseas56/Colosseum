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
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?Updates\.markSeen\(\)') {
    throw 'opening Update does not mark the release seen'
}
if ($main -notmatch 'id:\s*updateLayer') { throw 'Main.qml has no updateLayer loader' }

Write-Host 'test_update_taskbar_p0: PASS (icon, badge, pulse, accessibility, and shell route contract)'

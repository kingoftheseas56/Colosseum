# Update topbar notification P0: static contract for the home topbar Update glyph.
# The Update button no longer lives in the taskbar dock — it took the retired
# search slot on the home topbar. This test verifies the new single entry point
# and the full-bleed Update page's on-page status strip.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$topbar = Get-Content (Join-Path $root 'qml/TopBar.qml') -Raw
$bar = Get-Content (Join-Path $root 'qml/Taskbar.qml') -Raw
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$page = Get-Content (Join-Path $root 'qml/UpdatePage.qml') -Raw
$iconPath = Join-Path $root 'assets/icons/update.svg'

if (!(Test-Path $iconPath)) { throw 'missing update.svg' }
$svg = Get-Content $iconPath -Raw
if ($svg -match 'currentColor') { throw 'update.svg uses currentColor' }
if ($svg -notmatch 'stroke\s*=\s*"#[0-9a-fA-F]{6}"|fill\s*=\s*"#[0-9a-fA-F]{6}"') {
    throw 'update.svg has no explicit concrete stroke/fill'
}

# ---- TopBar: Update glyph + badge (home only) ----
if ($topbar -notmatch 'signal updateClicked') { throw 'TopBar missing signal updateClicked' }
foreach ($contract in @('property bool updateAvailable', 'property bool updateUnseen',
                        'property bool reducedMotion')) {
    if ($topbar -notmatch [regex]::Escape($contract)) { throw "TopBar missing $contract" }
}
if ($topbar -notmatch 'objectName:\s*"colosseumTopbarUpdateButton"') {
    throw 'home topbar Update glyph has no stable objectName'
}
if ($topbar -notmatch 'objectName:\s*"colosseumTopbarUpdateBadge"') {
    throw 'home topbar Update badge has no stable objectName'
}
# Search is gated to worlds only; Update glyph is home only.
if ($topbar -notmatch 'source:\s*"\.\./assets/icons/search\.svg"[\s\S]*?visible:\s*bar\.activeMedium\s*!==\s*""') {
    throw 'search SysIcon is not gated to worlds only'
}
if ($topbar -notmatch 'visible:\s*bar\.activeMedium\s*===\s*""') {
    throw 'Update glyph is not gated to home only'
}
if ($topbar -notmatch 'visible:\s*bar\.updateAvailable[\s\S]*?SequentialAnimation[\s\S]*?running:\s*bar\.updateUnseen\s*&&\s*bar\.updateAvailable\s*&&\s*!bar\.reducedMotion') {
    throw 'topbar update badge pulse is not guarded by unseen + available + reduced motion'
}

# ---- Taskbar: no Update button, no dead plumbing ----
if ($bar -match 'colosseumUpdateTaskbarButton') { throw 'Taskbar still has the Update launcher button' }
if ($bar -match 'colosseumUpdateBadge') { throw 'Taskbar still has the Update badge' }
if ($bar -match 'signal updateClicked|property bool updateActive|updatePresentation') {
    throw 'Taskbar retains dead update plumbing'
}
if ($bar -notmatch 'closedWidth:\s*130') { throw 'closed taskbar geometry changed outside scope' }

# ---- Main.qml: home TopBar wired + taskbar has no update bindings ----
if ($main -notmatch 'onUpdateClicked:\s*\(guideLayer\.active') {
    throw 'Main.qml does not wire the home TopBar Update glyph toggle'
}
if ($main -notmatch 'updateAvailable:\s*typeof Updates' -or
    $main -notmatch 'updateUnseen:\s*typeof Updates') {
    throw 'Main.qml does not bind Updates service to the home TopBar'
}
# The taskbar instance must NOT carry update bindings anymore.
$taskbarBlock = [regex]::Match($main, '(?ms)Taskbar\s*\{[\s\S]*?^\s*\}').Value
if ($taskbarBlock -match 'updateActive|updateAvailable|updateUnseen|updatePresentation|onUpdateClicked|onUpdatePrimaryActionRequested') {
    throw 'Main.qml Taskbar instance still carries dead update bindings'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?updateLayer\.active\s*=\s*true') {
    throw 'Main.qml has no openUpdatePage route'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?taskbar\.open\s*=\s*false') {
    throw 'opening Update does not close the taskbar for the full-bleed chronicle'
}
if ($main -notmatch 'function openUpdatePage\(\)[\s\S]*?Updates\.markSeen\(\)') {
    throw 'opening Update does not mark the release seen'
}
if ($main -notmatch 'id:\s*updateLayer') { throw 'Main.qml has no updateLayer loader' }
$immersiveBlock = [regex]::Match($main, 'readonly property bool immersiveSurfaceOpen:[^\r\n]*(?:\r?\n\s*\|\|[^\r\n]*)*').Value
if ($immersiveBlock -match [regex]::Escape('updateLayer.active')) {
    throw 'UpdatePage still suppresses the ordinary taskbar while the chronicle is open'
}
if ($main -notmatch 'property bool reducedMotion') {
    throw 'Main.qml has no reducedMotion property'
}

# ---- UpdatePage: on-page status strip carries the absorbed UI ----
if ($page -notmatch 'objectName:\s*"colosseumUpdateStatusStrip"') {
    throw 'UpdatePage has no status strip item'
}
foreach ($contract in @('objectName: "colosseumUpdateStatusText"',
                        'objectName: "colosseumUpdateStatusMetadata"',
                        'objectName: "colosseumUpdateProgress"',
                        'objectName: "colosseumUpdateProgressText"',
                        'objectName: "colosseumUpdateProgressTrack"',
                        'objectName: "colosseumUpdatePrimaryAction"')) {
    if ($page -notmatch [regex]::Escape($contract)) {
        throw "UpdatePage is missing on-page updater contract: $contract"
    }
}
if ($page -match 'UpdateStatusRail|colosseumUpdateStatusRail') {
    throw 'UpdatePage retains the rejected secondary status surface'
}
if (Test-Path (Join-Path $root 'qml/update/UpdateStatusRail.qml')) {
    throw 'rejected UpdateStatusRail.qml component still exists'
}
if ($page -notmatch 'taskbarPresentation') {
    throw 'UpdatePage does not expose the single updater presentation mapping'
}
if ($page -notmatch 'objectName:\s*"colosseumUpdatePrimaryAction"[\s\S]{0,1800}?height:\s*44') {
    throw 'page update primary action does not expose a 44px logical hit target'
}

# ---- Gallery contracts (unchanged) ----
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
foreach ($contract in @('readonly property real automationStageOpacity',
                        'readonly property bool automationStageSettled',
                        'property int automationPresentedFrames')) {
    if ($gallery -notmatch [regex]::Escape($contract)) {
        throw "gallery visual-readiness contract is missing named settled-stage automation: $contract"
    }
}
if ($gallery -notmatch 'readonly property bool visualContentReady:[\s\S]*?automationStageSettled') {
    throw 'gallery visual-readiness contract does not require the named stage-settled condition'
}
if ($gallery -notmatch 'Connections\s*\{[\s\S]*?target:\s*root\.Window\.window[\s\S]*?function onFrameSwapped\(\)[\s\S]*?automationPresentedFrames') {
    throw 'gallery visual-readiness contract does not fence readiness on completed window frame swaps'
}
if ($gallery -notmatch 'automationPresentedFrames\s*>?=\s*2[\s\S]*?automationVisualReady\s*=\s*true') {
    throw 'gallery visual-readiness contract does not require two completed frame swaps'
}

Write-Host 'test_update_taskbar_p0: PASS (topbar glyph + badge, no taskbar Update button, full-bleed page status strip, and shell route contract)'

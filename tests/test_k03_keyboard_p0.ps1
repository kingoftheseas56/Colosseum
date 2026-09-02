$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Read-Rel([string]$rel) {
    $path = Join-Path $root $rel
    if (-not (Test-Path $path)) { throw "missing $rel" }
    return Get-Content -Raw $path
}
function Need([string]$rel, [string]$needle, [string]$why) {
    $text = Read-Rel $rel
    if (-not $text.Contains($needle)) { throw "${rel}: $why" }
}
function NeedCount([string]$rel, [string]$needle, [int]$count, [string]$why) {
    $text = Read-Rel $rel
    $actual = ([regex]::Matches($text, [regex]::Escape($needle))).Count
    if ($actual -lt $count) { throw "${rel}: $why (found $actual, need >= $count)" }
}

Need 'qml\SettingsPage.qml' 'KeyboardScrollController' 'Settings must be keyboard scrollable.'
Need 'qml\SettingsPage.qml' 'accessibleName: "Show explicit content"' 'Explicit-content toggle needs semantic keyboard activation.'
Need 'qml\OpenRecentPanel.qml' 'Keys.onEscapePressed' 'Open Recent needs Esc dismissal.'
Need 'qml\OpenRecentPanel.qml' 'movePopupFocus' 'Open Recent must trap Tab within the popup.'
NeedCount 'qml\OpenRecentPanel.qml' 'KeyboardAction {' 2 'Recent rows and Clear need semantic keyboard actions.'
Need 'qml\CalendarPage.qml' 'id: comingKeys' 'Coming Up must be a horizontal keyboard collection.'
Need 'qml\CalendarPage.qml' 'id: monthGrid' 'Calendar month must be a spatial keyboard region.'
Need 'qml\CalendarPage.qml' 'id: dayListFocus' 'Selected-day rows need vertical keyboard traversal.'
NeedCount 'qml\ExtensionsSources.qml' 'KeyboardAction {' 5 'Source reorder/toggle/remove/configure actions need keyboard twins.'
Need 'qml\PersonalizePage.qml' 'id: laneRow' 'Personalize lanes need one arrow-driven focus region.'
Need 'qml\PersonalizePage.qml' 'id: gridKeys' 'Personalize tiles need grid navigation.'

Need 'qml\VaultPage.qml' 'openFocusedGridContextMenu' 'Vault cards need keyboard context parity.'
Need 'qml\VaultPage.qml' 'Qt.Key_Menu' 'Menu key must open the Vault card context menu.'
Need 'qml\VaultPage.qml' 'Qt.Key_F10' 'Shift+F10 must open the Vault card context menu.'
Need 'qml\VaultPage.qml' 'id: sortItemKey' 'Vault sort menu needs trapped keyboard actions.'
Need 'qml\VaultPage.qml' 'id: filterKindVideoChip' 'Vault filter panel needs a deterministic first keyboard item.'
Need 'qml\VaultPage.qml' 'id: continueKeys' 'Vault Continue rail must be arrow navigable.'
Need 'qml\VaultPage.qml' 'id: nextUpKeys' 'Vault Next Up rail must be arrow navigable.'
Need 'qml\VaultBrowseRail.qml' 'openRowMenuFromKeyboard(rootRow)' 'Vault storage row Menu/Shift+F10 must open the same overflow menu.'
Need 'qml\VaultBrowseRail.qml' 'ignoreField.forceActiveFocus' 'Ignore editor must move focus inside on open.'
Need 'qml\VaultConfirmCard.qml' 'id: pickKey' 'Founding-card kind picker choices need keyboard activation.'
Need 'qml\VaultDetailSheet.qml' 'id: playKey' 'Vault detail primary action needs semantic keyboard activation.'
Need 'qml\VaultIdentifyDialog.qml' 'id: resultKeys' 'Identify results need composite keyboard navigation.'
Need 'qml\VaultIdentityCeremonyDialog.qml' 'id: sameKey' 'Identity ceremony choices need trapped focus.'
Need 'qml\VaultDoor.qml' 'vaultDoorKeyboardAction' 'Taskbar Vault door needs keyboard activation.'
Need 'qml\VaultHomeWidget.qml' 'vaultHomeWidgetKeyboardAction' 'Vault home portal needs keyboard activation.'
Need 'qml\VaultBrowseEmpty.qml' 'Clear Vault filters' 'Vault empty-state recovery action needs keyboard activation.'
Need 'qml\VaultBrowseCrumb.qml' 'Keys.onHomePressed' 'Breadcrumb needs first-segment navigation.'

Need 'qml\WallpaperSearch.qml' 'id: animatedKeys' 'Animated wallpaper shelf needs horizontal keyboard navigation.'
Need 'qml\WallpaperSearch.qml' 'id: nativeKeys' 'Native wallpaper shelf needs horizontal keyboard navigation.'
Need 'qml\WallpaperSearch.qml' 'id: kdeKeys' 'KDE wallpaper shelf needs horizontal keyboard navigation.'
Need 'qml\WallpaperSearch.qml' 'id: resultKeys' 'Wallpaper results need grid keyboard navigation.'
Need 'qml\WallpaperSearch.qml' 'accessibleName: "Load more wallpapers"' 'Load More needs keyboard activation.'
Need 'qml\WallpaperSearch.qml' 'Apply wallpaper ' 'Wallpaper Apply choices need keyboard activation.'

Need 'qml\WindowBehavior.qml' 'sequence: "Alt+F7"' 'Window move gesture needs a keyboard twin.'
Need 'qml\WindowBehavior.qml' 'sequence: "Alt+F8"' 'Window resize gesture needs a keyboard twin.'
Need 'qml\WindowBehavior.qml' 'sequence: "Alt+F10"' 'Window maximize gesture needs a keyboard twin.'

foreach ($rel in @(
    'tests\qml\tst_open_recent_panel.qml','tests\qml\tst_vault_browse_rail_storage.qml',
    'tests\qml\tst_vault_card.qml','tests\qml\tst_vault_detail_sheet.qml',
    'tests\qml\tst_vault_door.qml','tests\qml\tst_vault_home_widget.qml',
    'tests\qml\tst_vault_identify_dialog.qml','tests\qml\tst_vault_identity_dialogs.qml'
)) { if (-not (Test-Path (Join-Path $root $rel))) { throw "missing $rel" } }

Write-Host 'K03_KEYBOARD_P0_OK'
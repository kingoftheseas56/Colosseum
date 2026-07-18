$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}

# --- Task 1: WorldTabBar is a generic, parameterized glass tab bar ---
$wtb = Get-Content (Join-Path $root "qml/WorldTabBar.qml") -Raw
Assert-Contains $wtb 'property var tabModel' "tab bar takes its tabs as a model (not hardcoded)"
Assert-Contains $wtb 'signal tabRequested(string tab)' "tab bar emits the selected tab key"
Assert-Contains $wtb 'property string currentTab' "tab bar highlights the active tab"
Assert-Contains $wtb 'tabs.tabModel.length' "pill width divides by the tab count, not a hardcoded 3"
Assert-Absent  $wtb ') / 3' "no hardcoded 3-tab division survives the generalization"

Write-Host "tankoban tabs contract OK"

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read([string]$rel) { Get-Content -Raw -LiteralPath (Join-Path $root $rel) }
function Has([string]$text, [string]$needle, [string]$label) {
    if (-not $text.Contains($needle)) { throw "missing capture surface: $label" }
}

$tanko = Read "qml/TankobanWorld.qml"
Has $tanko 'objectName: "tankobanWorld"' "Tankoban root"
Has $tanko 'tabPrefix: "tankobanTab"' "Tankoban tab namespace"

$theatre = Read "qml/TheatreWorld.qml"
Has $theatre 'objectName: "theatreWorld"' "Theatre root"
Has $theatre 'objectName: "theatreDiscoverPage"' "Theatre discover page"
Has $theatre 'objectName: "theatreCatalogPage"' "Theatre catalogue page"

$worldTabs = Read "qml/WorldTabBar.qml"
Has $worldTabs 'readonly property bool activeState: pill.modelData.key === tabs.currentTab' "WorldTabBar active-state seam"

$tabs = Read "qml/TheatreTabBar.qml"
Has $tabs 'objectName: "theatreTabBar"' "Theatre tab bar"
Has $tabs 'objectName: "theatreTab_movies"' "Theatre tab pills"
Has $tabs 'readonly property bool activeState: tabs.currentTab === "movies"' "Theatre active-state seam"

$account = Read "qml/account/AccountCenter.qml"
Has $account 'objectName: "accountCenterRail_" + modelData.id' "Account Centre rail"

$reader = Read "qml/reader2/Harness.qml"
Has $reader 'objectName: "reader2HarnessShell"' "Reader2 capture shell"
$shelf = Read "qml/reader2/HarnessShelf.qml"
Has $shelf 'objectName: "reader2HarnessBook_" + index' "Reader2 capture book"
Write-Host "LANISTA_CAPTURE_SURFACES_OK"

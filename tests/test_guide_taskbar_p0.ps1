# Living Guide Task 4 — static contract for the shared taskbar Guide door and the Main.qml
# shell route. Covers the wiring a Quick Test cannot reach in isolation: the monochrome icon,
# the stable objectName/accessible name, the expanded-taskbar visibility, the z:59 shell loader
# over the utility pages, the underlay-underline masking, and Escape-before-underlay ordering.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bar = Get-Content (Join-Path $root 'qml/Taskbar.qml') -Raw
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

# ---- the icon: concrete monochrome, never color or currentColor ----
$iconPath = Join-Path $root 'assets/icons/guide.svg'
if (!(Test-Path $iconPath)) { throw 'missing guide.svg' }
$svg = Get-Content $iconPath -Raw
if ($svg -match 'currentColor') { throw 'guide.svg uses currentColor' }
if ($svg -notmatch 'stroke\s*=\s*"#[0-9a-fA-F]{6}"|fill\s*=\s*"#[0-9a-fA-F]{6}"') {
    throw 'guide.svg has no explicit concrete stroke/fill'
}
if ($svg -match '(?i)gold|#f0c44a|0\.94,\s*0\.77,\s*0\.29') { throw 'guide.svg carries a retired gold literal' }

# ---- Taskbar door contract ----
foreach ($contract in @('signal guideClicked', 'property bool guideActive')) {
    if ($bar -notmatch [regex]::Escape($contract)) { throw "Taskbar missing $contract" }
}
if ($bar -notmatch 'objectName:\s*"colosseumGuideTaskbarButton"') {
    throw 'guide taskbar button has no stable objectName'
}
if ($bar -notmatch 'objectName:\s*"colosseumGuideUnderline"') {
    throw 'guide active underline has no stable objectName'
}
$guideBlock = [regex]::Match($bar, '(?ms)// ---- Guide:.*?(?=^\s*// ---- Settings:)').Value
if ([string]::IsNullOrWhiteSpace($guideBlock)) { throw 'could not isolate the Guide taskbar block' }
if ($guideBlock -notmatch 'visible:\s*bar\.open') {
    throw 'guide door is not part of the expanded taskbar composition'
}
if ($guideBlock -notmatch 'source:\s*"\.\./assets/icons/guide\.svg"') {
    throw 'guide door does not use the guide.svg icon'
}
if ($guideBlock -notmatch 'visible:\s*bar\.guideActive[\s\S]*?height:\s*3') {
    throw 'guide active state has no standard underline'
}
if ($guideBlock -notmatch 'Accessible\.name:\s*"Guide"') {
    throw 'guide door does not expose the accessible name "Guide"'
}
if ($guideBlock -match '(?i)gold|#f0c44a|0\.94,\s*0\.77,\s*0\.29') {
    throw 'guide taskbar block carries a retired gold literal'
}
if ($guideBlock -notmatch 'onClicked:\s*bar\.guideClicked\(\)') {
    throw 'guide door click does not emit guideClicked'
}
if ($bar -notmatch 'closedWidth:\s*130') { throw 'closed taskbar geometry changed outside Task 4 scope' }

# ---- Main.qml shell route ----
if ($main -notmatch 'id:\s*guideLayer') { throw 'Main.qml has no guideLayer loader' }
if ($main -notmatch '(?ms)id:\s*guideLayer[\s\S]{0,200}?z:\s*59') {
    throw 'guideLayer does not sit at z:59 (above the z:56 taskbar utility pages)'
}
if ($main -notmatch '(?ms)id:\s*guideLayer[\s\S]{0,400}?source:\s*"guide/GuidePage\.qml"') {
    throw 'guideLayer does not load guide/GuidePage.qml'
}
if ($main -notmatch 'function openGuidePage\(') { throw 'Main.qml has no openGuidePage route' }
if ($main -notmatch 'function closeGuidePage\(') { throw 'Main.qml has no closeGuidePage route' }
if ($main -notmatch 'function openGuidePage\([\s\S]*?guideLayer\.active\s*=\s*true') {
    throw 'openGuidePage does not activate the guide layer'
}
if ($main -notmatch 'function openGuidePage\([\s\S]*?taskbar\.open\s*=\s*true') {
    throw 'openGuidePage does not pin the expanded taskbar'
}

# ---- taskbar wiring: guideActive is driven, and the OTHER underlines are masked while Guide is open ----
if ($main -notmatch 'guideActive:\s*guideLayer\.active') {
    throw 'Main.qml does not drive the taskbar guideActive underline'
}
if ($main -notmatch 'onGuideClicked:') { throw 'Main.qml does not route onGuideClicked' }
foreach ($u in @('downloads', 'vault', 'extensions', 'settings', 'update')) {
    if ($main -notmatch "${u}Active:\s*${u}Layer\.active\s*&&\s*!guideLayer\.active") {
        throw "Main.qml does not mask the $u underline while Guide is open"
    }
}
# opening any other taskbar destination closes Guide first
foreach ($u in @('openDownloadsPage', 'openVaultPage', 'openExtensionsPage', 'openSettingsPage', 'openUpdatePage')) {
    if ($main -notmatch "function $u\([\s\S]*?guideLayer\.active\s*=\s*false") {
        throw "$u does not close Guide before following its own route"
    }
}
# CLICK-ROUTING (Preflight F2): while Guide floats, each taskbar destination routes through its open()
# (dismiss Guide + reveal/switch), NEVER the raw close branch — else the same-underlay click closes the
# page the user meant to return to and leaves Guide up. Inspect the ACTUAL click branch, not open*Page bodies.
$routes = [ordered]@{ Downloads = 'downloads'; Vault = 'vault'; Extensions = 'extensions'; Settings = 'settings'; Update = 'update' }
foreach ($name in $routes.Keys) {
    $u = $routes[$name]
    if ($main -notmatch "on${name}Clicked:\s*\(guideLayer\.active\s*\|\|\s*!${u}Layer\.active\)\s*\?\s*win\.open${name}Page") {
        throw "$name taskbar click does not give Guide-dismissal priority (same-underlay click would close the returned-to page)"
    }
}

# ---- Escape ownership: the shell Escape STANDS DOWN while Guide is open. Two Escape shortcuts active
#      on one window are an ambiguous overload that Qt resolves by firing NEITHER (Escape would go
#      dead), so GuidePage's own Escape must be the sole Escape while the Guide floats. ----
if ($main -notmatch 'Shortcut \{ sequences: \["Escape"\];\s*enabled:\s*!guideLayer\.active') {
    throw 'shell Escape does not stand down while Guide is open (would ambiguously collide with GuidePage Escape)'
}
# F1 (Preflight): UpdatePage is the ONE utility page that owns its own Escape; while Guide floats it must
# yield that Escape so Guide's is the sole enabled shortcut (two enabled Escapes on one window fire neither).
$update = Get-Content (Join-Path $root 'qml/UpdatePage.qml') -Raw
if ($update -notmatch 'property bool guideActive') { throw 'UpdatePage has no guideActive gate' }
if ($update -notmatch 'sequence:\s*"Escape"[\s\S]{0,120}?enabled:\s*!root\.guideActive') {
    throw 'UpdatePage Escape shortcut is not gated on !root.guideActive'
}
if ($update -notmatch 'Keys\.onEscapePressed:\s*if\s*\(!root\.guideActive\)') {
    throw 'UpdatePage Keys Escape is not gated on !root.guideActive'
}
if ($main -notmatch 'item\.guideActive\s*=\s*Qt\.binding') {
    throw 'Main.qml does not bind UpdatePage.guideActive to guideLayer.active'
}

# ---- the one shell seam later House contextual links consume (optional, guarded) ----
if ($main -notmatch 'item\.guideRequested') {
    throw 'Main.qml does not pre-wire the optional guideRequested seam on the utility loaders'
}
if ($main -notmatch 'item\.guideRequested\.connect') {
    throw 'guideRequested seam is declared but never connected to openGuidePage'
}

Write-Host 'test_guide_taskbar_p0: PASS (icon, door, z:59 shell route, underline masking, and Escape ordering contract)'

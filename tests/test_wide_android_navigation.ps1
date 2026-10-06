param([string]$RootOverride = "")
$ErrorActionPreference = "Stop"

$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$mainPath = Join-Path $root "qml\Main.qml"
$manifestPath = Join-Path $root "native\platform\android\AndroidManifest.xml"
$cmakePath = Join-Path $root "tests\CMakeLists.txt"
foreach ($path in @($mainPath, $manifestPath, $cmakePath)) {
    if (-not (Test-Path $path)) { throw "MISSING FILE: $path" }
}
$main = Get-Content $mainPath -Raw
$manifest = Get-Content $manifestPath -Raw
$cmake = Get-Content $cmakePath -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main.Contains('win.androidHost && win.width < 840 && !win.immersiveSurfaceOpen && !win.phoneImeVisible')) `
    'Narrow Android navigation must remain below the shared 840 dp breakpoint.'
Need ($main.Contains('win.androidHost && win.width >= 840 && !win.immersiveSurfaceOpen && !win.phoneImeVisible')) `
    'Wide Android navigation must own the same breakpoint without a gap.'
Need ($main.Contains('win.wideWorldNavigationVisible ? wideWorldRail.width : 0')) `
    'Wide shell content must reserve the actual rail width only while the rail is visible.'

$registryBindings = ([regex]::Matches($main, 'worlds:\s*win\.worldDefinitions')).Count
Need ($registryBindings -ge 2) `
    'Both narrow and wide Android navigation must use the one worldDefinitions registry.'
$activeBindings = ([regex]::Matches($main, 'activeTab:\s*downloadsLayer\.active \? "Profile" : worldStack\.current')).Count
Need ($activeBindings -ge 2) `
    'Both Android navigation presentations must share the same active-tab authority.'

Need ($main.Contains('anchors.leftMargin: win.wideWorldNavigationLeftInset')) `
    'World/profile content must reserve the wide rail instead of rendering underneath it.'
Need ($main.Contains('x: win.wideWorldNavigationLeftInset + adaptive.pageMargin')) `
    'Home top chrome must begin to the right of the wide rail.'
Need ($main.Contains('width: page.width - adaptive.pageMargin * 2')) `
    'Home content width must derive from the rail-adjusted viewport.'

Need ($manifest.Contains('android:screenOrientation="unspecified"')) `
    'The activity must remain rotation-capable; S1-E switches shell presentation from live viewport width.'
Need (-not $manifest.Contains('android:screenOrientation="portrait"')) `
    'S1-E must not lock the Android shell to portrait.'

Need ($cmake.Contains('add_test(NAME colosseum.wide_android_navigation_contract')) `
    'CTest must register the S1-E wide-navigation contract.'
Need ($cmake.Contains('test_wide_android_navigation.ps1')) `
    'CTest registration must invoke this exact S1-E contract script.'

Write-Host "wide Android navigation contract: PASS"

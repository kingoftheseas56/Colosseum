param([string]$RootOverride = "")
$ErrorActionPreference = "Stop"

$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$mainPath = Join-Path $root "qml\Main.qml"
$manifestPath = Join-Path $root "native\platform\android\AndroidManifest.xml"
if (-not (Test-Path $mainPath)) { throw "MISSING FILE: qml/Main.qml" }
if (-not (Test-Path $manifestPath)) { throw "MISSING FILE: native/platform/android/AndroidManifest.xml" }
$main = Get-Content $mainPath -Raw
$manifest = Get-Content $manifestPath -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($manifest.Contains('android:windowSoftInputMode="adjustResize"')) `
    'QtActivity must resize its Android window above the software keyboard.'
Need (-not $manifest.Contains('adjustPan')) `
    'Android must not pan the whole Qt surface under the IME.'

Need ($main.Contains('readonly property bool phoneImeVisible: win.androidHost && Qt.inputMethod.visible')) `
    'Main.qml must expose the Android-only IME visibility authority.'
Need ($main.Contains('win.androidHost && win.width < 840 && !win.immersiveSurfaceOpen && !win.phoneImeVisible')) `
    'Phone navigation must disappear while the IME owns the reduced viewport.'
Need ($main.Contains('return phoneWorldTabs.visible ? phoneWorldTabs.height : 0')) `
    'Content inset must continue to follow the actual tab-bar visibility, including IME suppression.'

Write-Host "phone keyboard avoidance contract: PASS"

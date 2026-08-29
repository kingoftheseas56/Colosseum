$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'native/main.cpp') -Raw
$qmlMain = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main.Contains('qEnvironmentVariableIntValue("COLOSSEUM_WORLD_WARMER") == 1')) `
    'World warming must be opt-in only when COLOSSEUM_WORLD_WARMER=1.'
Need ($main.Contains('setContextProperty(QStringLiteral("DevWorldWarmer")')) `
    'The native startup path must expose the world-warmer opt-in as DevWorldWarmer.'
Need ($qmlMain.Contains('readonly property bool worldWarmerEnabled')) `
    'Main must derive a QML world-warmer gate from the native opt-in flag.'
Need ($qmlMain.Contains('running: win.worldWarmerEnabled')) `
    'Automatic world warming must be disabled unless the explicit opt-in is enabled.'
Need ($qmlMain.Contains('function openWorld(medium)')) `
    'Explicit world navigation must remain available independently of automatic warming.'

Write-Host 'World warmer containment contract: PASS'

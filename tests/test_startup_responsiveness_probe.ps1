$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'native/main.cpp') -Raw
$probe = Get-Content (Join-Path $root 'native/GuiStallProbe.h') -Raw
$qmlMain = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$search = Get-Content (Join-Path $root 'qml/SearchSurface.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($probe.Contains('COLOSSEUM_STARTUP_PROBE')) `
    'GuiStallProbe must expose the opt-in startup qualification probe.'
Need ($main.Contains('app.markFirstFrame()')) `
    'The first frame boundary must be recorded by the native startup path.'
Need ($main.Contains('findChild<QQuickItem*>(QStringLiteral("bootSplash"))') -or
      $main.Contains('findChild<QObject*>(QStringLiteral("bootSplash"))')) `
    'The native startup path must locate the named BootSplash boundary.'
Need ($main.Contains('app.markShellInteractive()')) `
    'The shell-interactive boundary must be recorded after BootSplash visibility ends.'
Need ($main.Contains('app.setStallContext(')) `
    'GUI stall reports must carry explicit startup context while boot is active.'
Need ($main.Contains('app.setStallContext(QStringLiteral("startup"), QStringLiteral("qml-load"))')) `
    'The QML load boundary must be attributed as startup work.'

$frame = $main.IndexOf('frameSwapped')
$first = $main.IndexOf('app.markFirstFrame()', $frame)
$ack = $main.IndexOf('acknowledgeHealthyBoot(launchArguments)', $frame)
Need ($frame -ge 0 -and $first -gt $frame -and $ack -gt $first) `
    'First-frame measurement must precede the healthy-boot acknowledgement.'

$boot = $main.IndexOf('findChild<QQuickItem*>(QStringLiteral("bootSplash"))')
if ($boot -lt 0) { $boot = $main.IndexOf('findChild<QObject*>(QStringLiteral("bootSplash"))') }
$interactive = $main.IndexOf('app.markShellInteractive()', $boot)
Need ($boot -ge 0 -and $interactive -gt $boot) `
    'Shell interactivity must be wired from the BootSplash boundary.'
Need ($main.Contains('if (!bootSplash->isVisible())') -or
      $main.Contains('if (!bootSplash->property("visible").toBool())')) `
    'Already-hidden BootSplash instances must still emit the shell-interactive milestone.'

Need ($probe.Contains('class GuiStallProbeBridge')) `
    'The probe must expose a small QObject bridge for QML context updates.'
Need ($probe.Contains('Q_INVOKABLE void setContext')) `
    'The QML probe bridge must provide an invokable operation/surface setter.'
Need ($main.Contains('setContextProperty(QStringLiteral("GuiStallProbe")')) `
    'main.cpp must expose the probe bridge to QML.'
Need ($qmlMain.Contains('GuiStallProbe.setContext(operation, surface)')) `
    'Main must route explicit operation/surface context updates through the probe bridge.'
Need ($qmlMain.Contains('setGuiStallContext("navigate", String(medium))')) `
    'World navigation must attribute its active surface to the GUI stall probe.'
Need ($qmlMain.Contains('setGuiStallContext("open", "Downloads")')) `
    'Downloads opening must attribute its surface to the GUI stall probe.'
Need ($qmlMain.Contains('setGuiStallContext("open", "Player")')) `
    'Player opening must attribute its surface to the GUI stall probe.'
Need ($qmlMain.Contains('setGuiStallContext("open", "Reader")')) `
    'Reader opening must attribute its surface to the GUI stall probe.'
Need ($search.Contains('GuiStallProbe.setContext("search", surf.searchMode)')) `
    'Search dispatch must attribute its world surface to the GUI stall probe.'

Write-Host 'Startup responsiveness probe contract: PASS'

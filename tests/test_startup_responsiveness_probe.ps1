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

# BiblioCatalog is cache-first, but refreshIfDue() still performs synchronous SQLite/enqueue
# bookkeeping before issuing its asynchronous requests. Keep that coordinator off engine.load().
$engineLoad = $main.IndexOf('engine.load(QUrl::fromLocalFile(qmlPath));')
$biblioRefresh = $main.IndexOf('biblioCatalog->refreshIfDue();')
Need ($engineLoad -ge 0 -and $biblioRefresh -gt $engineLoad) `
    'Biblio refresh must be initiated after engine.load and first-frame wiring.'
Need ($main.Contains('QTimer::singleShot(0, biblioCatalog, [biblioCatalog]')) `
    'Biblio refresh must be queued after first-frame notification.'

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

# Below-the-fold Home intro widgets must not be instantiated as part of Main.qml's
# pre-first-frame object tree. They are loaded from one post-frame gate so their
# provider requests and poster delegates cannot extend the startup critical path.
Need ($qmlMain.Contains('function loadHomeIntroWidgets()')) `
    'Home intro widgets must have an explicit post-frame loading function.'
Need ($qmlMain.Contains('function armHomeIntroWidgets()')) `
    'Home intro widgets must have a named shell-ready arming gate.'
Need ($qmlMain.Contains('function armStartupIdleWork()')) `
    'Post-frame work must have a named idle gate after the splash boundary.'
Need ($qmlMain.Contains('id: startupIdleGate')) `
    'Startup work must be delayed by an explicit idle timer.'
Need ($qmlMain.Contains('startupIdleGate.start()')) `
    'The first-frame/splash paths must arm the delayed startup work gate.'
Need ($qmlMain.Contains('function homeIntroWidgetSettled(loader)')) `
    'Home intro widgets must advance through one settled asynchronous Loader at a time.'
Need ($qmlMain.Contains('homeIntroPendingLoader')) `
    'Home intro loading must track the single Loader currently incubating.'
Need ($qmlMain.Contains('if (!boot.visible) win.armStartupIdleWork()')) `
    'Home intro loading must wait until the boot splash no longer occludes the shell.'
Need ($qmlMain.Contains('homeIntroWidgetsTimer')) `
    'Home intro widgets must be armed by a named post-frame timer.'
Need ($qmlMain.Contains('homeIntroWidgetsTimer.start()')) `
    'The first-frame path must arm the deferred Home intro widget load.'
Need ($qmlMain.Contains('"Bookshelf.qml", "TheatreStrip.qml"')) `
    'The Home intro widget source list must include the Bookshelf and Theatre loaders.'
Need ($qmlMain.Contains('"ReadingDesk.qml", "VaultHomeWidget.qml"')) `
    'The Home intro widget source list must include the ReadingDesk and Vault loaders.'
Need ($qmlMain.Contains('loader.setSource(sources[homeIntroWidgetCursor - 1]')) `
    'The Home intro source must be assigned through the selected deferred Loader.'
Need ($qmlMain.Contains('loader.active = true')) `
    'The selected Home intro Loader must activate after its deferred source is assigned.'

# The Home Continue rail must keep its visual footprint while deferring the synchronous Progress
# snapshot and ContinueTile delegate construction until after the shell idle boundary.
Need ($qmlMain.Contains('function armHomeContinueRail()')) `
    'The Home Continue rail must have a named shell-ready arming gate.'
Need ($qmlMain.Contains('function loadHomeContinueRail()')) `
    'The Home Continue rail must have an explicit deferred loading function.'
Need ($qmlMain.Contains('id: homeContinueRailTimer')) `
    'The Home Continue rail must use a named post-splash timer.'
Need ($qmlMain.Contains('id: homeContinueRailLoader')) `
    'The Home Continue rail must be owned by a deferred Loader.'
Need ($qmlMain.Contains('homeContinueRailPlaceholderHeight: 192')) `
    'The Home Continue rail must reserve its existing fixed-height footprint before loading.'
Need ($qmlMain.Contains('homeContinueRailLoader.sourceComponent = homeContinueRailComponent')) `
    'The Home Continue rail source must be assigned only from its deferred loading function.'
Need ($qmlMain.Contains('property var contItems: (Progress.revision')) `
    'The deferred Continue rail must preserve its live Progress.revision binding.'
Need ($qmlMain.Contains('model: contCol.contItems')) `
    'The deferred Continue rail must preserve the ContinueTile repeater model.'
Need ($qmlMain.Contains('onResumeRequested: win.resumeContinue(modelData)')) `
    'The deferred Continue rail must preserve resume routing.'
Need ($qmlMain.Contains('onDetailRequested: win.detailContinue(modelData)')) `
    'The deferred Continue rail must preserve detail routing.'
Need ($qmlMain.Contains('onRemoveRequested: Progress.forget(modelData.kind, modelData.id)')) `
    'The deferred Continue rail must preserve remove routing.'

Need ($qmlMain.Contains('property bool auxiliaryFontsReady: false')) `
    'Player/reader-only fonts must have an explicit post-splash readiness gate.'
Need ($qmlMain.Contains('source: win.auxiliaryFontsReady ? "../assets/fonts/Switzer-Regular.otf" : ""')) `
    'Switzer must not load on the pre-first-frame Main.qml path.'
Need ($qmlMain.Contains('source: win.auxiliaryFontsReady ? "../assets/fonts/Inter-Regular.otf" : ""')) `
    'Inter must not load on the pre-first-frame Main.qml path.'
Need ($qmlMain.Contains('source: win.auxiliaryFontsReady ? "../assets/fonts/Literata-Regular.ttf" : ""')) `
    'Literata must not load on the pre-first-frame Main.qml path.'

Write-Host 'Startup responsiveness probe contract: PASS'

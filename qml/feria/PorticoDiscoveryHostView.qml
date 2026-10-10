import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../account" as AccountUi
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    objectName: "feriaBrowserHost"
    required property var controller
    readonly property bool retained: controller.browserDeck && controller.managedBrowserRoute
    readonly property var browserItem: retained ? controller.browserDeck.activeBrowser : browserLoader.item
    readonly property var pageTools: engine === "qtwebengine" && browserItem ? browserItem.pageTools || null : null
    FeriaBrowserMenu {
        id: browserMenu
        parent: Overlay.overlay
        deck: root.controller.browserDeck
        accountStore: root.controller.accountStore
        providerId: root.controller.hostApp
        currentTitle: root.pageTitle
        currentUrl: root.currentUrl
        onNavigateRequested: (url) => root.navigate(url)
        onFocusAddressRequested: navigationBar.focusAddress()
        onDownloadsRequested: if (deck) deck.showDownloads()
        onPrintRequested: if (root.browserItem && root.browserItem.browserRequests) root.browserItem.browserRequests.printPage()
        onMessage: (text) => root.controller.setToast(text)
    }
    function showBrowserTools(section) {
        root.optionsOpen = false
        browserMenu.openSection(section || "tabs")
    }
    function cycleBrowserTab(step) {
        var deck = root.controller.browserDeck
        if (!deck) return
        var records = deck.sessions.browsers()
        if (!records.length) return
        var current = records.findIndex(function(record) { return record.id === deck.sessions.activeId })
        deck.activate(records[(current + step + records.length) % records.length].id)
    }
    Shortcut {
        sequence: "Ctrl+T"
        enabled: root.visible && root.retained
        onActivated: {
            root.controller.browserDeck.newSpace(root.controller.hostSessionId, browserMenu.privateBrowsing, true)
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+N"
        enabled: root.visible && root.retained
        onActivated: {
            root.controller.browserDeck.newSpace(root.controller.hostSessionId, true, true)
        }
    }
    Shortcut { sequence: "Ctrl+W"; enabled: root.visible && root.retained; onActivated: root.controller.browserDeck.closeTab(root.controller.hostSessionId) }
    Shortcut { sequence: "Ctrl+Tab"; enabled: root.visible && root.retained; onActivated: root.cycleBrowserTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled: root.visible && root.retained; onActivated: root.cycleBrowserTab(-1) }
    Shortcut { sequence: "Ctrl+Shift+T"; enabled: root.visible && root.retained; onActivated: root.controller.browserDeck.restoreClosedTab() }
    Shortcut { sequence: "Ctrl+H"; enabled: root.visible && root.retained; onActivated: root.showBrowserTools("history") }
    Shortcut { sequence: "Ctrl+D"; enabled: root.visible && root.retained; onActivated: root.showBrowserTools("bookmarks") }
    Shortcut { sequence: "Ctrl+J"; enabled: root.visible && root.retained; onActivated: root.controller.browserDeck.showDownloads() }
    Shortcut { sequence: "Ctrl+P"; enabled: root.visible && root.retained && root.browserItem; onActivated: root.browserItem.browserRequests.printPage() }
    FeriaFindBar {
        id: findPanel
        parent: root
        tools: root.pageTools
        backdrop: root.controller.backdrop
        width: Math.min(382, Math.max(0, root.width - 24))
        x: Math.max(12, root.width - width - Math.min(24, root.controller.marginX))
        y: (root.fullScreenActive ? 0 : strip.height) + 8
    }
    Connections {
        target: root.pageTools
        function onFindOpenChanged() {
            if (root.pageTools.findOpen) { navigationBar.hideAddress(); root.optionsOpen = false }
        }
    }
    readonly property real chromeHeight: strip.height
    readonly property rect viewportRect: {
        root.width; root.height; root.x; root.y; strip.height
        var origin = browserLoader.mapToItem(null, 0, 0)
        return Qt.rect(origin.x, origin.y, browserLoader.width, browserLoader.height)
    }
    function preferredEngine(provider) { return enginePreferences[provider] || "qtwebengine" }
    function applyRetainedOptions() {
        if (!retained || !browserItem) return
        browserItem.appLayout = appOptions.layout !== false
        browserItem.appKeyboard = appOptions.keyboard !== false
        browserItem.appAccent = String(controller.gold)
    }
    onAppOptionsChanged: applyRetainedOptions()
    function syncRetainedState() {
        if (!retained || !browserItem) return
        applyRetainedOptions()
        currentUrl = String(browserItem.url)
        pageTitle = browserItem.title
        loadSucceeded = browserItem.documentReady
        loading = browserItem.loadActive
        errorText = controller.browserDeck.activeError
    }
    onBrowserItemChanged: syncRetainedState()
    Connections {
        target: root.controller.browserDeck
        function onActiveErrorChanged() { root.syncRetainedState() }
    }
    readonly property real u: controller.unit
    property string engineOverride: ""
    property var enginePreferences: ({})
    property var appPreferences: ({})
    property bool optionsOpen: false
    readonly property var appOptions: appPreferences[controller.hostApp] || ({layout:true,keyboard:true})
    function setAppOption(name,value) {
        var all = JSON.parse(JSON.stringify(appPreferences))
        var option = {layout:appOptions.layout !== false,keyboard:appOptions.keyboard !== false}
        option[name] = value
        all[controller.hostApp] = option
        appPreferences = all
        preferences.appsJson = JSON.stringify(all)
    }
    function focusApp() {
        optionsOpen = false
        navigationBar.hideAddress()
        if (!root.browserItem) return
        if (root.engine === "webview2") root.browserItem.focusWebView()
        else root.browserItem.forceActiveFocus()
    }
    function showOptions() {
        if (fullScreenActive) return
        navigationBar.hideAddress()
        if (root.pageTools) root.pageTools.closeFind(false)
        optionsOpen = true
        if (root.browserItem && engine === "webview2") root.browserItem.focusAppControls()
        Qt.callLater(function() { appHomeButton.forceActiveFocus() })
    }
    function findFromOptions() {
        root.optionsOpen = false
        Qt.callLater(function() { if (root.pageTools && root.pageTools.active) root.pageTools.openFind() })
    }
    function navigate(address) {
        var prefs = root.controller.accountStore ? root.controller.accountStore.browserPreferences : ({})
        var url = String(FeriaBrowserPolicy.omniboxUrl(address, prefs.searchEngine || "duckduckgo"))
        if (!url.length) {
            controller.setToast("Enter a web address or search")
            return
        }
        if (!root.browserItem) return
        if (retained) {
            controller.browserDeck.navigateInput(controller.hostSessionId, address)
            focusApp()
            return
        }
        controller.hostResumePosition = 0
        controller.hostResumeLocator = null
        root.browserItem.cancelMediaResume(true)
        root.browserItem.resumePosition = 0
        root.browserItem.resumeLocator = null
        if (root.engine === "qtwebengine") root.browserItem.wakePage()
        root.browserItem.url = url
        focusApp()
    }
    function cancelAddressEditing() { return navigationBar.cancelAddressEditing() }
    function pageBack() {
        if (retained) controller.browserDeck.back(controller.hostSessionId)
        else if (root.browserItem && engineCanGoBack) root.browserItem.pageBack()
        focusApp()
    }
    function pageForward() {
        if (retained) controller.browserDeck.forward(controller.hostSessionId)
        else if (root.browserItem && engineCanGoForward) root.browserItem.pageForward()
        focusApp()
    }
    function reloadOrStop() {
        if (!root.browserItem || !browserReady) return
        if (retained) {
            if (pageLoading) controller.browserDeck.stop(controller.hostSessionId)
            else controller.browserDeck.reload(controller.hostSessionId)
        }
        else if (pageLoading) root.browserItem.stopLoad()
        else root.browserItem.reload()
        focusApp()
    }
    function exitFullScreen() {
        if (retained && root.browserItem && !root.browserItem.fullScreenActive
            && controller.browserDeck.restoreSession(controller.hostSessionId)) return
        if (!root.browserItem) return
        var script = "if(document.fullscreenElement) document.exitFullscreen()"
        if (engine === "webview2") root.browserItem.executeScript("fullscreen-exit",script)
        else root.browserItem.runJavaScript(script)
    }
    function appHome() {
        var provider = Data.P[controller.hostApp]
        if (root.browserItem && provider && provider.d) {
            controller.hostResumePosition = 0
            controller.hostResumeLocator = null
            root.browserItem.cancelMediaResume(true)
            root.browserItem.resumePosition = 0
            root.browserItem.resumeLocator = null
            root.browserItem.url = "https://" + provider.d
            focusApp()
        }
    }
    readonly property string engine: engineOverride || enginePreferences[controller.hostApp]
        || "qtwebengine"
    property string currentUrl: controller.hostUrl
    property bool loading: false
    property bool loadSucceeded: false
    property string errorText: ""
    property string pageTitle: ""
    function receivePlayback(sample) {
        if (!sample || !controller.accountStore || !controller.recording) return
        pageTitle = sample.title || ""
        // Script results come from the browser's current top document, never page messages.
        if (FeriaBrowserPolicy.allowsNavigation(sample.href)) {
            currentUrl = sample.href
            if (!root.retained) controller.accountStore.observe(sample)
        }
    }
    readonly property bool browserReady: root.browserItem ? root.browserItem.ready : false
    readonly property bool fullScreenActive: (root.browserItem && root.browserItem.fullScreenActive === true)
        || (retained && controller.browserDeck.activeEntry && controller.browserDeck.activeEntry.immersive)
    // Engine-reported navigation state. History comes from engine APIs/events,
    // never a manually maintained URL stack. loadProgress is a real percentage
    // when the engine exposes one and -1 when it does not (indeterminate).
    readonly property bool engineCanGoBack: root.browserItem ? root.browserItem.navCanGoBack === true : false
    readonly property bool engineCanGoForward: root.browserItem ? root.browserItem.navCanGoForward === true : false
    readonly property bool pageLoading: root.browserItem
        ? (root.browserItem.loadActive === true) : root.loading
    readonly property real pageProgress: root.browserItem
        && typeof root.browserItem.loadProgress === "number"
        ? root.browserItem.loadProgress : -1
    Settings {
        id: preferences
        category: "FeriaBrowser"
        property string enginesJson: "{}"
        property string appsJson: "{}"
    }
    Component.onCompleted: {
        try { enginePreferences = JSON.parse(preferences.enginesJson) } catch (e) { enginePreferences = ({}) }
        try { appPreferences = JSON.parse(preferences.appsJson) } catch (e) { appPreferences = ({}) }
    }
    onVisibleChanged: if (visible) {
        engineOverride = ""
        currentUrl = controller.hostUrl
        errorText = ""
        loadSucceeded = false
        optionsOpen = false
        syncRetainedState()
    }
    function tryOtherBrowser() {
        if (!FeriaBrowserPolicy.webView2Available) return
        var resume = controller.continueItems.find(function(item) { return item.url === root.currentUrl })
        controller.hostResumePosition = resume && resume.kind !== "book" ? resume.position : 0
        controller.hostResumeLocator = resume ? resume.locator || null : null
        var next = engine === "webview2" ? "qtwebengine" : "webview2"
        var values = JSON.parse(JSON.stringify(enginePreferences))
        values[controller.hostApp] = next
        enginePreferences = values
        preferences.enginesJson = JSON.stringify(values)
        engineOverride = next
        if (retained) {
            // Keep the existing compatibility route. WebView2 remains a single
            // legacy host; its native retention semantics are outside this slice.
            var url = currentUrl
            var provider = controller.hostApp
            var title = controller.hostTitle
            var mode = controller.hostMode
            var position = controller.hostResumePosition
            var locator = controller.hostResumeLocator
            controller.browserParkRequested(true)
            var epoch = controller.navigationEpoch
            Qt.callLater(function() {
                if (controller.navigationEpoch !== epoch || !controller.lifecycleActive) return
                controller.openHost(provider, title, mode, url, position, locator)
            })
        }
        errorText = ""
        loadSucceeded = false
    }
    component BrowserAction: AccountUi.AccountButton {
        implicitWidth: Math.max(42, contentItem.implicitWidth + 28)
        implicitHeight: 36
        Layout.minimumWidth: implicitWidth
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.visible && root.retained && root.controller.browserDeck.activeEntry
            && root.controller.browserDeck.activeEntry.immersive
            && !(root.browserItem && root.browserItem.fullScreenActive)
            && !(root.pageTools && root.pageTools.findOpen)
        onActivated: root.exitFullScreen()
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.visible && !root.fullScreenActive
        onActivated: navigationBar.focusAddress()
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: root.visible && !root.fullScreenActive && root.engineCanGoBack
        onActivated: root.pageBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: root.visible && !root.fullScreenActive && root.engineCanGoForward
        onActivated: root.pageForward()
    }
    Shortcut {
        sequences: ["Ctrl+R", "F5"]
        enabled: root.visible && !root.fullScreenActive && root.browserReady
        onActivated: {
            if (root.retained) root.controller.browserDeck.reload(root.controller.hostSessionId)
            else root.browserItem.reload()
            root.focusApp()
        }
    }
    Popup {
        id: optionsPanel
        objectName: "feriaOptionsPanel"
        parent: root
        width: Math.min(260, Math.max(0, root.width - 24))
        height: optionsContent.implicitHeight + 24
        padding: 12
        x: Math.max(12, Math.min(root.width - width - 12,
            optionsButton.mapToItem(root, optionsButton.width, 0).x - width))
        y: strip.height + 8
        visible: root.optionsOpen && root.visible && !root.fullScreenActive
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.optionsOpen = false
        property bool siteControlsView: false
        onOpened: { siteControlsView = false; appHomeButton.forceActiveFocus() }
        background: Colosseum.Glass {
            backdrop: root.controller.backdrop
            contentBackdrop: root.browserItem && root.engine === "qtwebengine" ? root.browserItem : null
            contentTrack: root.browserItem && root.engine === "qtwebengine" ? root.browserItem.scrollPosition.y : 0
            track: optionsPanel.x + optionsPanel.y
            radius: 18; tint: 0.08; scrim: 0.66
        }
        Shortcut { sequence: "Ctrl+F"; enabled: optionsPanel.visible && root.pageTools !== null; onActivated: root.findFromOptions() }
        Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: optionsPanel.visible && root.pageTools !== null; onActivated: root.pageTools.zoomIn() }
        Shortcut { sequence: "Ctrl+-"; enabled: optionsPanel.visible && root.pageTools !== null; onActivated: root.pageTools.zoomOut() }
        Shortcut { sequence: "Ctrl+0"; enabled: optionsPanel.visible && root.pageTools !== null; onActivated: root.pageTools.resetZoom() }
        Shortcut { sequence: "F10"; enabled: optionsPanel.visible; onActivated: root.focusApp() }
        contentItem: ColumnLayout {
            id: optionsContent
            spacing: 10
            Colosseum.KeyboardSpatialNavigator { id: optionsNav; root: optionsContent }
            Keys.onPressed: function(event) { optionsNav.handle(event) }
            RowLayout {
                visible: !optionsPanel.siteControlsView
                Layout.fillWidth: true
                spacing: 8
                FeriaPageToolButton {
                    objectName: "feriaNewBrowserSession"
                    iconSource: "../../assets/icons/lucide/minus.svg"; crossBar: true
                    helpText: "New session"
                    visible: !!root.controller.browserDeck && root.engine === "qtwebengine"
                    onClicked: { root.optionsOpen = false; root.controller.openHost(root.controller.hostApp, "", "home", "", 0, null, true) }
                }
                FeriaPageToolButton {
                    objectName: "feriaFindAction"
                    iconSource: "../../assets/icons/reader2/search.svg"
                    helpText: "Find in page (Ctrl+F)"
                    visible: root.pageTools !== null
                    enabled: root.pageTools && root.pageTools.active
                    onClicked: root.findFromOptions()
                }
                FeriaPageToolButton {
                    id: appHomeButton
                    objectName: "feriaAppHome"
                    iconSource: "../../assets/icons/reader2/return.svg"
                    helpText: "App home"
                    enabled: !!Data.P[controller.hostApp]
                    onClicked: { root.optionsOpen = false; root.appHome() }
                }
                Item { Layout.fillWidth: true }
                FeriaPageToolButton {
                    id: siteControlsButton
                    objectName: "feriaSiteControls"
                    iconSource: "../../assets/icons/lucide/sliders-horizontal.svg"
                    helpText: "Site controls"
                    onClicked: { optionsPanel.siteControlsView = true; siteBack.forceActiveFocus() }
                }
            }
            BrowserAction {
                objectName: "feriaBrowserTools"
                visible: root.retained && !optionsPanel.siteControlsView
                Layout.fillWidth: true
                text: "Tabs, bookmarks and browser settings"
                onClicked: root.showBrowserTools("tabs")
            }
            BrowserAction {
                objectName: "feriaBrowserDownloadsAction"
                visible: root.retained && !optionsPanel.siteControlsView
                Layout.fillWidth: true
                text: "Downloads"
                onClicked: { root.optionsOpen = false; root.controller.browserDeck.showDownloads() }
            }
            BrowserAction {
                objectName: "feriaBrowserResetPermissions"
                visible: root.retained && !optionsPanel.siteControlsView
                Layout.fillWidth: true
                text: "Reset this site's permissions"
                onClicked: {
                    root.optionsOpen = false
                    if (root.browserItem && root.browserItem.browserRequests)
                        root.browserItem.browserRequests.resetSitePermissions()
                }
            }
            ColumnLayout {
                visible: optionsPanel.siteControlsView
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    FeriaPageToolButton {
                        id: siteBack
                        objectName: "feriaSiteControlsBack"
                        iconSource: "../../assets/icons/lucide/arrow-left.svg"
                        helpText: "Back to options"
                        onClicked: { optionsPanel.siteControlsView = false; siteControlsButton.forceActiveFocus() }
                    }
                    Text { text: "Site controls"; color: root.controller.ink; font.family: root.controller.uiFont; font.pixelSize: 13 }
                }
                BrowserAction {
                    objectName: "feriaAppLayout"
                    Layout.fillWidth: true
                    text: "App layout: " + (root.appOptions.layout !== false ? "On" : "Off")
                    Accessible.checkable: true; Accessible.checked: root.appOptions.layout !== false
                    onClicked: root.setAppOption("layout", root.appOptions.layout === false)
                }
                BrowserAction {
                    objectName: "feriaAppKeyboard"
                    Layout.fillWidth: true
                    text: "Arrow navigation: " + (root.appOptions.keyboard !== false ? "On" : "Off")
                    Accessible.checkable: true; Accessible.checked: root.appOptions.keyboard !== false
                    onClicked: root.setAppOption("keyboard", root.appOptions.keyboard === false)
                }
                BrowserAction {
                    objectName: "feriaBrowserSwitch"
                    Layout.fillWidth: true
                    visible: FeriaBrowserPolicy.webView2Available
                    text: "Compatibility mode"
                    onClicked: { root.tryOtherBrowser(); root.optionsOpen = false }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.controller.ink; opacity: 0.12; visible: root.pageTools !== null && !optionsPanel.siteControlsView }
            FeriaZoomControls {
                Layout.fillWidth: true
                tools: root.pageTools
                visible: root.pageTools !== null && !optionsPanel.siteControlsView
            }
        }
    }
    Rectangle { anchors.fill: parent; anchors.topMargin: strip.height; color: "#08090d"; visible: !root.retained }
    Item {
        id: strip
        objectName: "feriaAppBar"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: !root.fullScreenActive
        height: root.fullScreenActive ? 0 : 52
        Colosseum.KeyboardSpatialNavigator { id: appNav; root:strip }
        Keys.onPressed: function(event) { appNav.handle(event) }
        RowLayout {
            anchors { left:parent.left; right:parent.right; top:parent.top }
            height:52
            anchors.leftMargin: controller.marginX
            anchors.rightMargin: controller.marginX
            spacing: strip.width < 1000 ? 6 : root.u
            Colosseum.BackAction {
                objectName: "feriaBrowserReturn"
                variant: "immersive"
                tip: "Back to Feria"
                idleColor: controller.mist
                hoverColor: controller.ink
                onTriggered: controller.back()
            }
            PorticoCombinedGlyph {
                Layout.preferredWidth: 1.3 * root.u
                Layout.preferredHeight: 1.3 * root.u
                glyphKey: Data.P[controller.hostApp] ? controller.hostApp : "portico"
                tone: controller.ink
            }
            Text {
                text: controller.providerName(controller.hostApp)
                Layout.maximumWidth: strip.width < 900 ? 120 : 240
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: 14
            }
            FeriaBrowserToolbar {
                id: navigationBar
                Layout.preferredWidth: implicitWidth
                Layout.minimumWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                currentUrl: root.currentUrl
                canGoBack: root.engineCanGoBack
                canGoForward: root.engineCanGoForward
                loading: root.pageLoading
                browserReady: root.browserReady
                onBackRequested: root.pageBack()
                onForwardRequested: root.pageForward()
                onReloadRequested: root.reloadOrStop()
                onAddressAccepted: function(address) { root.navigate(address) }
                onEditingCancelled: root.focusApp()
                onAddressEditingStarted: if (root.pageTools) root.pageTools.closeFind(false)
                onFindRequested: if (root.pageTools) root.pageTools.openFind()
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.errorText || (root.pageLoading ? "Opening " + controller.providerName(controller.hostApp) + "…" : controller.toast || "")
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 13
                elide: Text.ElideRight
                Accessible.role: Accessible.StatusBar
            }
            FeriaPageToolButton {
                objectName: "feriaSaveReadingPlace"
                iconSource: "../../assets/icons/reader2/bookmark.svg"
                helpText: "Save reading place"
                visible: controller.isReadingProvider(controller.hostApp)
                enabled: controller.recording && root.loadSucceeded
                onClicked: {
                    if (root.retained ? controller.browserDeck.savePlace()
                        : controller.accountStore.saveReadingPlace(root.currentUrl, root.pageTitle))
                        controller.setToast("Reading place saved to Continue")
                    else controller.setToast("This page cannot be saved. Open the book or chapter first.")
                }
            }
            FeriaPageToolButton {
                id: optionsButton
                objectName: "feriaAppOptions"
                iconSource: "../../assets/icons/lucide/sliders-horizontal.svg"
                helpText: root.optionsOpen ? "Close options (F10)" : "Options (F10)"
                variant: root.optionsOpen ? "secondary" : "link"
                onClicked: {
                    if (root.optionsOpen) root.focusApp()
                    else root.showOptions()
                }
            }
            Colosseum.Glass {
                objectName: "feriaSessionControlTile"
                Layout.preferredWidth: windowActions.implicitWidth + 6
                Layout.preferredHeight: 50
                backdrop: root.controller.backdrop
                radius: 11; tint: 0.04; scrim: 0.60
                FeriaWindowControls {
                    id: windowActions
                    anchors.centerIn: parent
                    controller: root.retained ? sessionActions : root.controller
                    sessionControls: root.retained
                    sessionFullscreen: root.fullScreenActive
                    compactSessionTile: true
                    namePrefix: "feriaHost"
                }
            }
        }
        // Engine load feedback: a real percentage where the engine reports one,
        // an honest indeterminate sweep where it does not.
        Rectangle {
            id: loadTrack
            objectName: "feriaLoadTrack"
            anchors { left: parent.left; right: parent.right }
            y: parent.height - height
            height: 2
            color: "#141824"
            visible: root.pageLoading
            clip: true
            Rectangle {
                id: loadFill
                objectName: "feriaLoadFill"
                height: parent.height
                color: controller.gold
                width: root.pageProgress >= 0
                    ? Math.max(2, parent.width * Math.min(100, Math.max(0, root.pageProgress)) / 100)
                    : parent.width * 0.3
                x: 0
                SequentialAnimation on x {
                    running: loadTrack.visible && root.pageProgress < 0
                    loops: Animation.Infinite
                    NumberAnimation { from: -loadFill.width; to: loadTrack.width; duration: 1100; easing.type: Easing.InOutQuad }
                }
            }
        }
    }
    QtObject {
        id: sessionActions
        function minimizeClicked() { root.controller.browserParkRequested(false) }
        function powerClicked() { root.controller.browserCloseRequested(root.controller.hostSessionId) }
        function fullscreenClicked() {
            if (root.fullScreenActive) root.exitFullScreen()
            else if (root.retained)
                root.controller.browserDeck.toggleAppMax(root.controller.hostSessionId)
        }
    }
    Loader {
        id: browserLoader
        objectName: "feriaBrowserLoader"
        anchors { left: parent.left; right: parent.right; top: strip.bottom; bottom: parent.bottom }
        active: !root.retained && root.visible && controller.lifecycleActive && controller.hostUrl.length > 0 && FeriaBrowserPolicy.storageRoot.length > 0
        onActiveChanged: if (active) loadBrowser()
        Connections {
            target: root
            function onEngineChanged() { if (browserLoader.active) browserLoader.loadBrowser() }
        }
        function loadBrowser() {
            controller.beginHostVisit()
            root.loading = true
            root.errorText = ""
            setSource(root.engine === "webview2" ? "FeriaWebView2.qml" : "FeriaQtWebEngine.qml", {
                sourceUrl: root.currentUrl || controller.hostUrl,
                profilePath: FeriaBrowserPolicy.storageRoot,
                providerId:controller.hostApp,
                accountVisitToken:controller.accountStore ? controller.accountStore.visitToken() : "",
                appLayout:Qt.binding(function() { return root.appOptions.layout !== false }),
                appKeyboard:Qt.binding(function() { return root.appOptions.keyboard !== false }),
                appAccent:String(controller.gold),
                resumePosition: controller.hostResumePosition,
                resumeLocator: controller.hostResumeLocator,
                observationMode: controller.providerMode(controller.hostApp),
                observationEnabled: Qt.binding(function() { return controller.recording }),
                suppressed: Qt.binding(function() { return controller.browserSuppressed })
            })
        }
        onStatusChanged: if (status === Loader.Error) {
            root.loading = false
            root.errorText = "The browser could not start."
            if (root.engine === "webview2") Qt.callLater(function() { root.engineOverride = "qtwebengine" })
        }
    }
    Connections {
        target: root.browserItem
        function onCaptureProtectionChanged(blocked, visitToken) {
            if (!root.retained && controller.accountStore) controller.accountStore.setMediaCaptureBlocked(blocked, visitToken)
        }
        function onPlaybackObserved(sample) { root.receivePlayback(sample) }
        // The page's own title. Playback samples carry one only while activity is
        // being recorded, so a private tab, or any tab with recording paused,
        // showed no title at all after it navigated.
        function onTitleChanged() { if (root.browserItem) root.pageTitle = root.browserItem.title }
        function onResumeUnavailable() { controller.setToast("The saved page is open. Use the provider's controls to restore your position.") }
        function onStarted(location) {
            FeriaBrowserPolicy.rememberOrigin(controller.hostApp, location)
            root.pageTitle = ""
            root.currentUrl = location
            root.loading = true
            root.loadSucceeded = false
            root.errorText = ""
            controller.providerWebViewReady = false
        }
        function onCompleted(location, success) {
            root.currentUrl = location
            root.loading = false
            root.loadSucceeded = success
            root.errorText = success ? "" : (FeriaBrowserPolicy.webView2Available
                ? "Could not open the app. Reload or try compatibility mode in Options."
                : "Could not open the app. Try Reload.")
            controller.providerWebViewReady = success
        }
        function onFailed(reason) {
            root.loading = false
            root.errorText = "The browser could not start."
            if (root.engine === "webview2") Qt.callLater(function() { root.engineOverride = "qtwebengine" })
        }
        function onExitRequested() { Qt.callLater(function() { controller.back() }) }
        function onOptionsRequested() { Qt.callLater(function() { root.showOptions() }) }
    }
}

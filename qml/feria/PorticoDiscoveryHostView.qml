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
    readonly property real u: controller.unit
    property string engineOverride: ""
    property var enginePreferences: ({})
    property var appPreferences: ({})
    property bool optionsOpen: false
    property var qtBrowserComponent: null
    readonly property bool qtAvailable: qtBrowserComponent && qtBrowserComponent.status === Component.Ready
    readonly property string startupBlockedMessage: visible && controller.lifecycleActive
        && !FeriaBrowserPolicy.storageRoot.length
        ? "The browser profile is not available yet. Return to Feria and try again." : ""
    readonly property bool recoveryVisible: !fullScreenActive
        && (startupBlockedMessage.length > 0 || recovery.error.length > 0 || recovery.slow || recovery.resumeMessage.length > 0 || recovery.stopped)
    function deferHost(action, protectNavigation) {
        var generation = recovery.epoch
        var visit = recovery.visitKey
        var selectedEngine = engine
        var item = browserLoader.item
        Qt.callLater(function() {
            if (root && root.visible && browserLoader.active && recovery.visitKey === visit
                    && root.engine === selectedEngine && browserLoader.item === item
                    && (protectNavigation === false || recovery.epoch === generation)) action()
        })
    }
    FeriaRecoveryHostState {
        id: recovery
        browser: browserLoader.item
        active: browserLoader.active
        engine: root.engine
        qtAvailable: root.qtAvailable
        webAvailable: FeriaBrowserPolicy.webView2Available
        visitKey: controller.hostApp + "|" + controller.hostUrl + "|" + FeriaBrowserPolicy.storageRoot
        onPlaybackSample: function(sample) { root.receivePlayback(sample) }
        onBrowserExitRequested: root.deferHost(function() { controller.back() })
        onBrowserOptionsRequested: root.deferHost(function() { root.showOptions() })
        onRestartRequested: browserLoader.loadBrowser()
        onSwitchRequested: function(next, remember) { root.switchBrowser(next, remember) }
        onLocationStarted: function(location) {
            FeriaBrowserPolicy.rememberOrigin(controller.hostApp, location)
            root.pageTitle = ""
            root.currentUrl = location
            controller.providerWebViewReady = false
        }
        onLocationCompleted: function(location, success) {
            root.currentUrl = location
            controller.providerWebViewReady = success
        }
    }
    onFullScreenActiveChanged: if (fullScreenActive) optionsOpen = false
    Connections {
        target: controller
        function onHostUrlChanged() { if (root.visible && browserLoader.active) restartVisit.restart() }
        function onHostAppChanged() { if (root.visible && browserLoader.active) restartVisit.restart() }
    }
    Connections {
        target: root.qtBrowserComponent
        function onStatusChanged() {
            if (browserLoader.active && root.engine === "qtwebengine" && !browserLoader.item
                    && root.qtBrowserComponent.status !== Component.Loading)
                browserLoader.loadBrowser()
        }
    }
    Connections {
        target: FeriaBrowserPolicy
        function onStorageRootChanged() {
            recovery.resetVisit()
            if (root.visible && browserLoader.active) restartVisit.restart()
        }
    }
    Timer {
        id: restartVisit
        interval: 0
        onTriggered: if (root.visible && browserLoader.active) {
            recovery.resetVisit()
            root.currentUrl = controller.hostUrl
            browserLoader.loadBrowser()
        }
    }
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
        if (!browserLoader.item) return
        deferHost(function() {
            if (!browserLoader.item || root.optionsOpen) return
            if (root.engine === "webview2") browserLoader.item.focusWebView()
            else browserLoader.item.forceActiveFocus()
        }, false)
    }
    function showOptions() {
        if (fullScreenActive) return
        optionsOpen = true
        if (browserLoader.item && engine === "webview2") browserLoader.item.focusAppControls()
        deferHost(function() { if (root.optionsOpen) appOptionsButton.forceActiveFocus() }, false)
    }
    function exitFullScreen() {
        if (!browserLoader.item) return
        var script = "if(document.fullscreenElement) document.exitFullscreen()"
        if (engine === "webview2") browserLoader.item.executeScript("fullscreen-exit",script)
        else browserLoader.item.runJavaScript(script)
    }
    function appHome() {
        var provider = Data.P[controller.hostApp]
        if (browserLoader.item && provider && provider.d) {
            recovery.resumeMessage = ""
            browserLoader.item.resumePosition = 0
            browserLoader.item.resumeLocator = null
            browserLoader.item.url = "https://" + provider.d
            focusApp()
        }
    }
    readonly property string engine: engineOverride || enginePreferences[controller.hostApp]
        || (FeriaBrowserPolicy.webView2Available ? "webview2" : "qtwebengine")
    property string currentUrl: controller.hostUrl
    readonly property bool loading: recovery.starting
    readonly property bool loadSucceeded: recovery.succeeded
    readonly property string errorText: recovery.error
    property string pageTitle: ""
    function receivePlayback(sample) {
        if (!sample || !controller.accountStore || !controller.recording) return
        pageTitle = sample.title || ""
        // Script results come from the browser's current top document, never page messages.
        if (FeriaBrowserPolicy.allowsNavigation(sample.href)) {
            currentUrl = sample.href
            controller.accountStore.observe(sample)
        }
    }
    readonly property bool browserReady: browserLoader.item ? browserLoader.item.ready : false
    readonly property bool fullScreenActive: browserLoader.item ? browserLoader.item.fullScreenActive === true : false
    // Engine-reported navigation state. History comes from engine APIs/events,
    // never a manually maintained URL stack. loadProgress is a real percentage
    // when the engine exposes one and -1 when it does not (indeterminate).
    readonly property bool engineCanGoBack: browserLoader.item ? browserLoader.item.navCanGoBack === true : false
    readonly property bool engineCanGoForward: browserLoader.item ? browserLoader.item.navCanGoForward === true : false
    readonly property bool pageLoading: recovery.busy
    readonly property real pageProgress: browserLoader.item
        && typeof browserLoader.item.loadProgress === "number"
        ? browserLoader.item.loadProgress : -1
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
    onVisibleChanged: {
        if (visible) {
            recovery.resetVisit()
            engineOverride = ""
            currentUrl = controller.hostUrl
            optionsOpen = false
        } else {
            recovery.cancel()
            restartVisit.stop()
            optionsOpen = false
        }
    }
    function switchBrowser(next, remember) {
        if (!visible || !browserLoader.active) return
        if ((next === "webview2" && !FeriaBrowserPolicy.webView2Available)
                || (next === "qtwebengine" && !qtAvailable)) return
        // Keep destination; wrapper-owned engine history is not synthesized or copied.
        if (remember) {
            var resume = controller.continueItems.find(function(item) { return item.url === root.currentUrl })
            controller.hostResumePosition = resume && resume.kind !== "book" ? resume.position : 0
            controller.hostResumeLocator = resume ? resume.locator || null : null
            var values = JSON.parse(JSON.stringify(enginePreferences))
            values[controller.hostApp] = next
            preferences.enginesJson = JSON.stringify(values)
            engineOverride = next
            enginePreferences = values
        } else engineOverride = next
    }
    function tryOtherBrowser() { recovery.switchEngine() }
    component BrowserAction: AccountUi.AccountButton {
        implicitWidth: Math.max(42, contentItem.implicitWidth + 28)
        implicitHeight: 36
        Layout.minimumWidth: implicitWidth
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (enabled) clicked()
                event.accepted = true
            } else event.accepted = false
        }
    }
    Rectangle { anchors.fill: parent; color: "#08090d" }
    Rectangle {
        id: strip
        objectName: "feriaAppBar"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: !root.fullScreenActive
        height: root.fullScreenActive ? 0 : controlsColumn.implicitHeight + 8
        color: "#08090d"
        Colosseum.KeyboardSpatialNavigator { id: appNav; root:strip }
        Keys.onPressed: function(event) { appNav.handle(event) }
        Column {
            id: controlsColumn
            anchors { left:parent.left; right:parent.right; top:parent.top; leftMargin:controller.marginX; rightMargin:controller.marginX }
            spacing: 8
        RowLayout {
            width: parent.width
            height:52
            spacing: 8
            Colosseum.BackAction {
                objectName: "feriaBrowserReturn"
                label: "Back to Feria"
                idleColor: controller.mist
                hoverColor: controller.ink
                labelSize: 14
                onTriggered: controller.back()
            }
            BrowserAction {
                id: previousPage
                objectName: "feriaBrowserBack"
                text: "Previous page"
                Accessible.name: "Previous page in provider history"
                enabled: root.engineCanGoBack
                onClicked: if (recovery.navigate(true)) root.focusApp()
            }
            PorticoCombinedGlyph {
                visible: root.width >= 1150
                Layout.preferredWidth: 1.3 * root.u
                Layout.preferredHeight: 1.3 * root.u
                glyphKey: Data.P[controller.hostApp] ? controller.hostApp : "portico"
                tone: controller.ink
            }
            Text {
                text: controller.providerName(controller.hostApp)
                visible: root.width >= 1150
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: 14
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
            BrowserAction {
                objectName: "feriaSaveReadingPlace"
                text: "Save place"
                visible: controller.isReadingProvider(controller.hostApp)
                enabled: controller.recording && root.loadSucceeded
                onClicked: {
                    if (controller.accountStore.saveReadingPlace(root.currentUrl, root.pageTitle))
                        controller.setToast("Reading place saved to Continue")
                    else controller.setToast("This page cannot be saved. Open the book or chapter first.")
                }
            }
            BrowserAction {
                objectName: "feriaBrowserReload"
                text: root.pageLoading ? "Stop" : "Reload"
                enabled: root.pageLoading ? recovery.canStop : recovery.canRetry
                onClicked: {
                    if (root.pageLoading) recovery.stop()
                    else recovery.retry()
                    root.focusApp()
                }
            }
            BrowserAction {
                objectName: "feriaAppOptions"
                text: root.optionsOpen ? "Done" : "Options"
                Accessible.name: root.optionsOpen ? "Close app options" : "App options"
                Accessible.description: "Press F10 for app options"
                onClicked: {
                    if (root.optionsOpen) root.focusApp()
                    else root.showOptions()
                }
            }
            FeriaWindowControls {
                controller: root.controller
                namePrefix: "feriaHost"
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: 44
            }
        }
        Flow {
            id:optionsFlow
            width: parent.width
            spacing:10
            visible:root.optionsOpen
            BrowserAction {
                objectName:"feriaBrowserForward"
                text:"Next page"
                enabled:root.engineCanGoForward
                onClicked: if (recovery.navigate(false)) root.focusApp()
            }
            BrowserAction {
                objectName:"feriaAppHome"
                text:"App home"
                enabled:root.browserReady && !!Data.P[controller.hostApp]
                onClicked:root.appHome()
            }
            BrowserAction {
                id: appOptionsButton
                objectName:"feriaAppLayout"
                text:"App layout: " + (root.appOptions.layout !== false ? "On" : "Off")
                onClicked:root.setAppOption("layout",root.appOptions.layout === false)
            }
            BrowserAction {
                objectName:"feriaAppKeyboard"
                text:"Arrow navigation: " + (root.appOptions.keyboard !== false ? "On" : "Off")
                onClicked:root.setAppOption("keyboard",root.appOptions.keyboard === false)
            }
            BrowserAction {
                objectName:"feriaBrowserSwitch"
                visible: recovery.alternativeAvailable
                enabled: recovery.alternativeAvailable && !recovery.actionPending
                text:"Compatibility mode"
                onClicked: { root.tryOtherBrowser(); root.optionsOpen = false }
            }
        }
        Column {
            width: parent.width
            visible: root.recoveryVisible
            spacing: 8
            Text {
                width: parent.width
                text: recovery.error || root.startupBlockedMessage || (recovery.slow
                    ? "This page is taking longer than usual. You can stop loading or return to Feria."
                    : recovery.stopped ? "Loading stopped. The page may be incomplete." : "")
                visible: text.length > 0
                color: controller.ink; font.family: controller.uiFont; font.pixelSize: 14
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StatusBar
            }
            FeriaRecoveryNotice {
                width: parent.width
                automationId: "feriaResumeUnavailable"
                message: recovery.resumeMessage
                actionText: "Dismiss position notice"
                onActionRequested: { recovery.resumeMessage = ""; root.focusApp() }
            }
            Flow {
                width: parent.width; spacing: 10
                BrowserAction {
                    objectName: "feriaRecoveryRetry"
                    text: browserLoader.item && root.browserReady ? "Retry page" : "Retry browser startup"
                    visible: recovery.error.length > 0 || recovery.stopped
                    enabled: recovery.canRetry
                    onClicked: recovery.retry()
                }
                BrowserAction {
                    objectName: "feriaRecoveryCompatibility"
                    text: "Compatibility mode"
                    visible: recovery.alternativeAvailable
                    enabled: recovery.alternativeAvailable && !recovery.actionPending
                    onClicked: recovery.switchEngine()
                }
                BrowserAction {
                    objectName: "feriaRecoveryReturn"
                    text: "Return to Feria"
                    onClicked: controller.back()
                }
            }
        }
        } // controlsColumn
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
    Loader {
        id: browserLoader
        objectName: "feriaBrowserLoader"
        // Native bounds signals can precede a change to the parent's position.
        // Restore visibility after the complete layout has settled.
        property bool layoutPending: false
        function settleNativeLayout() { layoutPending = false }
        function nativeLayoutChanged() {
            if (root.engine !== "webview2" || !item) return
            layoutPending = true
            Qt.callLater(settleNativeLayout)
        }
        onYChanged: nativeLayoutChanged()
        onHeightChanged: nativeLayoutChanged()
        anchors { left: parent.left; right: parent.right; top: strip.bottom; bottom: parent.bottom }
        active: root.visible && controller.lifecycleActive && controller.hostUrl.length > 0 && FeriaBrowserPolicy.storageRoot.length > 0
        onActiveChanged: {
            if (active) loadBrowser()
            else recovery.cancel()
        }
        Connections {
            target: root
            function onEngineChanged() { if (browserLoader.active) browserLoader.loadBrowser() }
        }
        function loadBrowser() {
            if (!active || !root.visible) return
            if (!root.qtBrowserComponent) root.qtBrowserComponent = Qt.createComponent("FeriaQtWebEngine.qml")
            source = "" // Dispose the old engine even if its replacement is unavailable.
            controller.beginHostVisit()
            recovery.beginStartup()
            if (root.engine === "qtwebengine" && root.qtBrowserComponent.status === Component.Loading) return
            if (!recovery.engineAvailable) {
                recovery.failed()
                return
            }
            setSource(root.engine === "webview2" ? "FeriaWebView2.qml" : "FeriaQtWebEngine.qml", {
                sourceUrl: root.currentUrl || controller.hostUrl,
                profilePath: FeriaBrowserPolicy.storageRoot,
                providerId:controller.hostApp,
                appLayout:Qt.binding(function() { return root.appOptions.layout !== false }),
                appKeyboard:Qt.binding(function() { return root.appOptions.keyboard !== false }),
                appAccent:String(controller.gold),
                resumePosition: controller.hostResumePosition,
                resumeLocator: controller.hostResumeLocator,
                observationMode: controller.providerMode(controller.hostApp),
                observationEnabled: Qt.binding(function() { return controller.recording }),
                suppressed: Qt.binding(function() { return controller.browserSuppressed || root.optionsOpen || browserLoader.layoutPending })
            })
        }
        onStatusChanged: if (status === Loader.Error) recovery.failed()
    }
}

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
        if (root.engine === "webview2") browserLoader.item.focusWebView()
        else browserLoader.item.forceActiveFocus()
    }
    function showOptions() {
        if (fullScreenActive) return
        optionsOpen = true
        if (browserLoader.item && engine === "webview2") browserLoader.item.focusAppControls()
        Qt.callLater(function() { appOptionsButton.forceActiveFocus() })
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
            browserLoader.item.resumePosition = 0
            browserLoader.item.resumeLocator = null
            browserLoader.item.url = "https://" + provider.d
            focusApp()
        }
    }
    readonly property string engine: engineOverride || enginePreferences[controller.hostApp]
        || (FeriaBrowserPolicy.webView2Available ? "webview2" : "qtwebengine")
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
            controller.accountStore.observe(sample)
        }
    }
    readonly property bool browserReady: browserLoader.item ? browserLoader.item.ready : false
    readonly property bool fullScreenActive: browserLoader.item ? browserLoader.item.fullScreenActive === true : false
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
        errorText = ""
        loadSucceeded = false
    }
    component BrowserAction: AccountUi.AccountButton {
        implicitWidth: Math.max(42, contentItem.implicitWidth + 28)
        implicitHeight: 36
        Layout.minimumWidth: implicitWidth
    }
    Rectangle { anchors.fill: parent; color: "#08090d" }
    Rectangle {
        id: strip
        objectName: "feriaAppBar"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: !root.fullScreenActive
        height: root.fullScreenActive ? 0 : root.optionsOpen ? 70 + optionsFlow.implicitHeight : 52
        color: "#08090d"
        Colosseum.KeyboardSpatialNavigator { id: appNav; root:strip }
        Keys.onPressed: function(event) { appNav.handle(event) }
        RowLayout {
            anchors { left:parent.left; right:parent.right; top:parent.top }
            height:52
            anchors.leftMargin: controller.marginX
            anchors.rightMargin: controller.marginX
            spacing: root.u
            Colosseum.BackAction {
                objectName: "feriaBrowserReturn"
                label: "Feria"
                idleColor: controller.mist
                hoverColor: controller.ink
                labelSize: 14
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
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: 14
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.errorText || (root.loading ? "Opening " + controller.providerName(controller.hostApp) + "…" : controller.toast || "")
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
            anchors { left:parent.left; right:parent.right; top:parent.top; topMargin:58; leftMargin:controller.marginX; rightMargin:controller.marginX }
            spacing:10
            visible:root.optionsOpen
            BrowserAction {
                id:appOptionsButton
                objectName:"feriaBrowserBack"
                text:"Previous page"
                onClicked: { if (browserLoader.item) browserLoader.item.goBack(); root.focusApp() }
            }
            BrowserAction {
                objectName:"feriaAppHome"
                text:"App home"
                enabled:!!Data.P[controller.hostApp]
                onClicked:root.appHome()
            }
            BrowserAction {
                objectName:"feriaBrowserReload"
                text:"Reload"
                onClicked: { if (browserLoader.item) browserLoader.item.reload(); root.focusApp() }
            }
            BrowserAction {
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
                visible:FeriaBrowserPolicy.webView2Available
                text:"Compatibility mode"
                onClicked: { root.tryOtherBrowser(); root.optionsOpen = false }
            }
        }
    }
    Loader {
        id: browserLoader
        objectName: "feriaBrowserLoader"
        anchors { left: parent.left; right: parent.right; top: strip.bottom; bottom: parent.bottom }
        active: root.visible && controller.lifecycleActive && controller.hostUrl.length > 0 && FeriaBrowserPolicy.storageRoot.length > 0
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
        target: browserLoader.item
        function onPlaybackObserved(sample) { root.receivePlayback(sample) }
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
                ? "Could not open the app. Use Options to reload or try compatibility mode."
                : "Could not open the app. Use Options to reload.")
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

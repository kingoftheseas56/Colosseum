import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: root
    objectName: "feriaBrowserHost"
    required property var controller
    readonly property real u: controller.unit
    property string engineOverride: ""
    property var enginePreferences: ({})
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
    Settings {
        id: preferences
        category: "FeriaBrowser"
        property string enginesJson: "{}"
    }
    Component.onCompleted: {
        try { enginePreferences = JSON.parse(preferences.enginesJson) } catch (e) { enginePreferences = ({}) }
    }
    onVisibleChanged: if (visible) {
        engineOverride = ""
        currentUrl = controller.hostUrl
        errorText = ""
        loadSucceeded = false
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
    Rectangle { anchors.fill: parent; color: "#08090d" }
    Rectangle {
        id: strip
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Math.max(46, 4.2 * root.u)
        color: "#08090d"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: controller.marginX
            anchors.rightMargin: controller.marginX
            spacing: root.u
            Button {
                objectName: "feriaBrowserReturn"
                text: "‹  Feria"
                onClicked: controller.back()
            }
            Button {
                objectName: "feriaBrowserBack"
                text: "‹"
                Accessible.name: "Previous page"
                onClicked: if (browserLoader.item) browserLoader.item.goBack()
            }
            Button {
                objectName: "feriaBrowserReload"
                text: "↻"
                Accessible.name: "Reload page"
                onClicked: if (browserLoader.item) browserLoader.item.reload()
            }
            PorticoCombinedGlyph {
                Layout.preferredWidth: 1.3 * root.u
                Layout.preferredHeight: 1.3 * root.u
                glyphKey: controller.hostApp || "plus"
                tone: controller.ink
            }
            Text {
                text: controller.providerName(controller.hostApp)
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: root.u
            }
            Text {
                Layout.fillWidth: true
                text: root.currentUrl
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.85 * root.u
                elide: Text.ElideMiddle
            }
            Button {
                objectName: "feriaSaveReadingPlace"
                text: "Save reading place"
                visible: controller.isReadingProvider(controller.hostApp)
                enabled: controller.recording && root.loadSucceeded
                onClicked: {
                    if (controller.accountStore.saveReadingPlace(root.currentUrl, root.pageTitle))
                        controller.setToast("Reading place saved to Continue")
                    else controller.setToast("This page cannot be saved. Open the book or chapter first.")
                }
            }
            Button {
                objectName: "feriaBrowserSwitch"
                visible: FeriaBrowserPolicy.webView2Available
                text: "Try other browser"
                onClicked: root.tryOtherBrowser()
            }
        }
    }
    Loader {
        id: browserLoader
        objectName: "feriaBrowserLoader"
        anchors { left: parent.left; right: parent.right; top: strip.bottom; bottom: parent.bottom; bottomMargin: 84 }
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
                ? "This page could not load. Reload it or try the other browser."
                : "This page could not load. Try reloading it.")
            controller.providerWebViewReady = success
        }
        function onFailed(reason) {
            root.loading = false
            root.errorText = "The browser could not start."
            if (root.engine === "webview2") Qt.callLater(function() { root.engineOverride = "qtwebengine" })
        }
        function onExitRequested() { Qt.callLater(function() { controller.back() }) }
    }
    Text {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 38; leftMargin: 260; rightMargin: controller.marginX }
        text: root.errorText || (root.loading ? "Loading…" : "")
        color: root.errorText ? controller.gold : controller.slate
        font.family: controller.uiFont
        font.pixelSize: 0.9 * root.u
        wrapMode: Text.WordWrap
    }
}

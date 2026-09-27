// ExtensionsWorldPage.qml — thin native host for the Svelte Extensions world.
// Chain, House and Store all live in worlds/extensions/ and render inside this WebEngineView.
// QML owns only the shell seam and QWebChannel bridge:
//   "extensions" = the app's ExtensionsStore (installed(), setEnabled(), changed)
//   "host"       = this page (back, universe hall, open an add-on setup page in the browser).
import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    objectName: "extensionsWorldPage"

    // Same surface as ExtensionsPage.qml, so Main.qml's extensionsLayer wiring is unchanged.
    property Item backdrop: null
    property bool showExplicit: false
    property string world: "theatre"
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal universeHallRequested()

    readonly property url worldUrl: Qt.resolvedUrl("../resources/worlds/extensions/index.html")
    property string loadStatus: "pending"

    Theme { id: theme }

    function takeKeyboardFocus() { web.forceActiveFocus() }
    // Main.qml asks first. The world decides: a Store category or search closes first, else it calls host.back().
    function requestEscape() {
        web.runJavaScript("window.extensionsBack ? window.extensionsBack() : null")
        return true
    }

    QtObject {
        id: host
        WebChannel.id: "host"
        function back() { root.backRequested() }
        function openUniverseHall() { root.universeHallRequested() }
        // An add-on's own setup page (configure, debrid key) opens in the user's browser.
        function openExternal(url) {
            if (String(url).indexOf("https://") === 0) Qt.openUrlExternally(url)
        }
    }

    Component.onCompleted: channel.registerObject("extensions", Extensions)

    WebEngineView {
        id: web
        objectName: "extensionsWorldView"
        anchors.fill: parent
        visible: true
        backgroundColor: "#06070a"
        focus: true
        webChannel: WebChannel { id: channel; registeredObjects: [host] }
        url: root.worldUrl
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true   // add-on logos are HTTPS
        onNavigationRequested: function(request) {
            if (request.url.toString().split("#")[0] !== root.worldUrl.toString())
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadSucceededStatus) { root.loadStatus = "ready"; web.forceActiveFocus() }
            else if (info.status === WebEngineView.LoadFailedStatus) root.loadStatus = "failed: " + info.errorString
        }
    }

    BackAction {
        visible: true
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }

}

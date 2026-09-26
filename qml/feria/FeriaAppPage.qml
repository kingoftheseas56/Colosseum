import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    objectName: "feriaAppPage"
    signal backRequested()

    readonly property url appUrl: Qt.resolvedUrl("../../resources/feria/index.html")
    property string loadStatus: "pending"

    function requestEscape() {
        web.runJavaScript("window.feriaBack && window.feriaBack()")
    }
    function takeKeyboardFocus() { web.forceActiveFocus() }

    Component.onCompleted: channel.registerObject("feria", porticoWebBridge)

    Connections {
        target: porticoWebBridge
        function onCloseRequested() { root.backRequested() }
    }

    WebEngineView {
        id: web
        objectName: "feriaWebView"
        anchors.fill: parent
        backgroundColor: "#0d0e12"
        focus: true
        webChannel: WebChannel { id: channel }
        url: root.appUrl
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true // live cover images are HTTPS
        onNavigationRequested: function(request) {
            if (request.url.toString().split("#")[0] !== root.appUrl.toString())
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                root.loadStatus = "ready"
                web.forceActiveFocus()
            } else if (info.status === WebEngineView.LoadFailedStatus) {
                root.loadStatus = "failed: " + info.errorString
            }
        }
    }
}

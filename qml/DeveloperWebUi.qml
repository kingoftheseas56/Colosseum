import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    objectName: "developerWebUi"
    focus: true

    function openRoute(route) {
        web.runJavaScript("if (window.CW && CW.router) CW.router.go(" + JSON.stringify(route) + ")")
    }

    WebChannel {
        id: channel
        Component.onCompleted: channel.registerObject("ColosseumWebBridge", ColosseumWebBridge)
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        focus: true
        backgroundColor: "#06070b"
        webChannel: channel
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        url: ColosseumWebBridge.resourceUrl

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadStartedStatus)
                ColosseumWebBridge.clearSurfaces()
            if (info.status === WebEngineView.LoadSucceededStatus)
                web.forceActiveFocus()
        }
    }
}

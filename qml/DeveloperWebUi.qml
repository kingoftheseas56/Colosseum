import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    objectName: "developerWebUi"
    focus: true

    WebChannel {
        id: channel
        Component.onCompleted: channel.registerObject("ColosseumWebBridge", DeveloperWebUiBridge)
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        focus: true
        backgroundColor: "#06070b"
        webChannel: channel
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        url: DeveloperWebUiBridge.resourceUrl

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadSucceededStatus)
                web.forceActiveFocus()
        }
    }
}

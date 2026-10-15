import QtQuick
import QtQuick.Window

Window {
    visible: true
    width: 300; height: 200
    Loader {
        id: l
        source: "../qml/DownloadsPage.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — page created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}

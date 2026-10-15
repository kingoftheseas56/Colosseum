import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: 320
    height: 220
    property int readyCount: 0

    function ready(name) {
        readyCount += 1
        console.log("[diag] LOADER READY — " + name)
        if (readyCount === 3) {
            console.log("[diag] ALL K04 REMAINING LOADERS READY")
            Qt.quit()
        }
    }

    Loader { source: "../qml/UniverseExtensionPage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("UniverseExtensionPage") }
    Loader { source: "../qml/LocgPublisherPage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("LocgPublisherPage") }
    Loader { source: "../qml/UniverseTile.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("UniverseTile") }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            console.log("[diag] TIMEOUT — ready " + win.readyCount + "/3")
            Qt.quit()
        }
    }
}

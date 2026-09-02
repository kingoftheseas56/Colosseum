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
        if (readyCount === 5) {
            console.log("[diag] ALL K04 REMAINING LOADERS READY")
            Qt.quit()
        }
    }

    Loader { source: "../qml/UniversePage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("UniversePage") }
    Loader { source: "../qml/UniverseExtensionPage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("UniverseExtensionPage") }
    Loader { source: "../qml/StudioUniversePage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("StudioUniversePage") }
    Loader { source: "../qml/LocgPublisherPage.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("LocgPublisherPage") }
    Loader { source: "../qml/UniverseTile.qml"; onStatusChanged: if (status === Loader.Ready) win.ready("UniverseTile") }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            console.log("[diag] TIMEOUT — ready " + win.readyCount + "/5")
            Qt.quit()
        }
    }
}

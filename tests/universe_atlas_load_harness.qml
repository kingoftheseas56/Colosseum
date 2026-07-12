// Lazy-Loader-style gate for UniverseAtlas — instantiates the hero component headless so a
// creation-time QML error can't ship invisibly. READY is the verdict.
import QtQuick
import QtQuick.Window

Window {
    visible: true
    width: 300; height: 200
    Loader {
        id: l
        source: "../qml/UniverseAtlas.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — atlas created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}

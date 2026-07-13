// Lazy-loader gate: boot smoke never enters universe pages, so instantiate the dedicated
// Cognitive Atlas with no universe name (no network) and require Loader.Ready.
import QtQuick
import QtQuick.Window

Window {
    visible: true
    width: 1280; height: 720
    Loader {
        id: pageLoader
        source: "../qml/CosmereUniversePage.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR")
            if (status === Loader.Ready) { console.log("[diag] LOADER READY"); Qt.quit() }
        }
    }
    Timer { interval: 5000; running: true; onTriggered: Qt.exit(3) }
}


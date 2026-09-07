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
            if (status === Loader.Ready) {
                var opened = null
                item.bookRequested.connect(function(book) { opened = book })
                item.openBook({ id: 101, title: "Mistborn", author: "Brandon Sanderson" })
                if (!opened || opened.id !== 101) { console.log("[diag] BOOK SIGNAL FAILED"); Qt.exit(4); return }
                item.openBook("Mistborn")
                if (opened.id !== 101) { console.log("[diag] BARE STRING LEAKED"); Qt.exit(5); return }
                console.log("[diag] LOADER READY — BOOK SIGNAL READY")
                Qt.quit()
            }
        }
    }
    Timer { interval: 5000; running: true; onTriggered: Qt.exit(3) }
}

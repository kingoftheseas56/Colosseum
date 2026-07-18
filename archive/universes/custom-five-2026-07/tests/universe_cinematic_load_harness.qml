// Lazy-Loader gate for CinematicPage — grew the TELEVISION ACT (SagaApi + Universes
// imports, TvShelf component) in the 2026-07-13 MCU expansion; boot smokes never
// instantiate it, this does (READY proves creation).
import QtQuick
import QtQuick.Window

Window {
    visible: true
    width: 300; height: 200
    Loader {
        id: l
        source: "../qml/CinematicPage.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — page created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}

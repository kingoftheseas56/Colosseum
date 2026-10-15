// Lazy-Loader gate for ContinueSeeAllPage — boot smokes are blind to creation-time
// errors behind an inactive Loader, so this instantiates the page for real.
// (`Progress` is a shell context property; absent under qml.exe it only produces
// binding warnings — creation still succeeds, which is exactly what READY proves.)
import QtQuick
import QtQuick.Window

Window {
    visible: true
    width: 300; height: 200
    Loader {
        id: l
        source: "../qml/ContinueSeeAllPage.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — page created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}

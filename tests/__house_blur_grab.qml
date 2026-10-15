import QtQuick
import QtQuick.Window
import "../qml" as UI
Window {
    width: 1706; height: 884; visible: true; color: "#05060a"
    UI.ExtensionsHousePage { id: page; anchors.fill: parent; universeIndex: 0 }
    Timer {
        interval: 2200; running: true; repeat: false
        onTriggered: page.grabToImage(function(result) {
            var ok = result.saveToFile("artifacts/house-qml-blur-proof.png")
            Qt.exit(ok ? 0 : 2)
        })
    }
}
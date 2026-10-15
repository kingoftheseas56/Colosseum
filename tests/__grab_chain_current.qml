import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    width: 1600
    height: 900
    visible: true
    color: "#05060a"

    UI.ExtensionsChainPage {
        id: page
        anchors.fill: parent
    }

    Timer {
        interval: 1200
        running: true
        repeat: false
        onTriggered: page.grabToImage(function(result) {
            var ok = result.saveToFile("artifacts/qml-chain-1600.png")
            Qt.exit(ok ? 0 : 2)
        })
    }
}

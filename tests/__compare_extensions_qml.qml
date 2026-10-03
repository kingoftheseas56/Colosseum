import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1600
    height: 900
    visible: true
    color: "#05060a"
    property string mode: "chain"

    Loader {
        id: page
        anchors.fill: parent
        sourceComponent: win.mode === "chain" ? chain : house
    }

    Component { id: chain; UI.ExtensionsChainPage { } }
    Component { id: house; UI.ExtensionsHousePage { universeIndex: 0 } }

    Timer {
        interval: 1800
        running: true
        onTriggered: page.item.grabToImage(function(r) {
            r.saveToFile("artifacts/compare-qml-chain.png")
            win.mode = "house"
            houseShot.start()
        })
    }

    Timer {
        id: houseShot
        interval: 1800
        onTriggered: page.item.grabToImage(function(r) {
            r.saveToFile("artifacts/compare-qml-house.png")
            Qt.exit(0)
        })
    }
}
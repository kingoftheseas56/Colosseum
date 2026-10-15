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

    Component {
        id: chain
        UI.ExtensionsChainPage {
            prototypeInstalled: ({ "colosseum.well.tankoyomi": true })
        }
    }

    Component {
        id: house
        UI.ExtensionsHousePage {
            universeIndex: 0
            localInstalled: ({ "colosseum.well.tankoyomi": true })
        }
    }

    Timer {
        interval: 1600
        running: true
        onTriggered: page.item.grabToImage(function(r) {
            r.saveToFile("artifacts/parity-qml-chain.png")
            win.mode = "house"
            houseShot.start()
        })
    }

    Timer {
        id: houseShot
        interval: 1600
        onTriggered: page.item.grabToImage(function(r) {
            r.saveToFile("artifacts/parity-qml-house.png")
            Qt.exit(0)
        })
    }
}
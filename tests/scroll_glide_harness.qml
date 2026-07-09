import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../qml"

Window {
    visible: true
    width: 400
    height: 400

    Flickable {
        id: f
        anchors.fill: parent
        contentHeight: 4000
        contentWidth: 400
    }

    Loader {
        id: ld
        source: "../qml/ScrollGlide.qml"
        onLoaded: item.flick = f
    }

    Flickable {
        id: barFlick
        width: 120
        height: 220
        contentWidth: width
        contentHeight: 900
        anchors.right: parent.right
        anchors.top: parent.top

        ScrollBar.vertical: HouseScrollBar {
            flick: parent
        }
    }

    Timer {
        interval: 1200
        running: true
        repeat: false
        onTriggered: {
            if (ld.status === Loader.Ready) {
                console.log("SCROLLGLIDE LOAD PASS")
                Qt.exit(0)
                return
            }
            console.log("SCROLLGLIDE LOAD FAIL: " + ld.status + " " + ld.source)
            Qt.exit(1)
            return
        }
    }
}

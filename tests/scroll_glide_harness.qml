import QtQuick
import QtQuick.Window

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

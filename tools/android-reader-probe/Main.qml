import QtQuick
import QtQuick.Controls

Window {
    id: root
    visible: true
    width: 390
    height: 844
    color: "#202020"
    property int taps: 0

    WindowContainer {
        id: paper
        anchors.fill: parent
        anchors.margins: 20
        anchors.topMargin: 80
        window: ReaderProbe.foreignWindow
    }
    // The overlay is a child window, not a same-window sibling painted behind WebView.
    WindowContainer {
        x: 30; y: 100
        width: Math.min(300, root.width - 60); height: 64
        z: 1
        window: Window {
            color: "#edc458"
            Button {
                anchors.fill: parent
                text: "QML overlay taps: " + root.taps
                onClicked: { root.taps++; console.info("reader-probe overlay tap", root.taps) }
            }
        }
    }
    Button {
        x: 20; y: 20; height: 44
        text: "Recreate WebView"
        onClicked: ReaderProbe.recreate()
    }
}

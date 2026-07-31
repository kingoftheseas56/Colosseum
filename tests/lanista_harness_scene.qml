// Deterministic scene for the lanista harness. Every interactive element is
// objectNamed — the bridge's whole targeting model rests on that discipline.
import QtQuick

Window {
    id: win
    objectName: "harnessWindow"
    width: 800; height: 600
    visible: true
    title: "LanistaHarness"

    property int clickCount: 0

    Rectangle { anchors.fill: parent; color: "#101218" }

    Rectangle {
        id: counterButton
        objectName: "counterButton"
        x: 100; y: 100; width: 200; height: 48
        radius: 8; color: ma.containsMouse ? "#2a3242" : "#1a2030"
        Text {
            objectName: "counterLabel"
            anchors.centerIn: parent
            text: "clicks: " + win.clickCount
            color: "#f0f0f0"
        }
        MouseArea {
            id: ma; objectName: "counterMouse"
            anchors.fill: parent; hoverEnabled: true
            onClicked: win.clickCount++
        }
    }

    TextInput {
        id: field
        objectName: "nameField"
        x: 100; y: 200; width: 200; height: 32
        color: "#f0f0f0"; font.pixelSize: 16
    }

    // An item partly OUTSIDE the window — geometry assertions must see this.
    Rectangle {
        objectName: "clippedBox"
        x: win.width - 40; y: 300; width: 120; height: 40; color: "#803030"
    }

    Flickable {
        objectName: "longList"
        x: 400; y: 100; width: 300; height: 400
        contentWidth: width; contentHeight: col.height
        Column {
            id: col
            Repeater {
                model: 40
                delegate: Rectangle {
                    required property int index
                    objectName: "row" + index
                    width: 300; height: 50
                    color: index % 2 ? "#181c26" : "#12151d"
                    Text { text: "row " + parent.index; color: "#c0c0c0"; x: 8; y: 14 }
                }
            }
        }
    }
}

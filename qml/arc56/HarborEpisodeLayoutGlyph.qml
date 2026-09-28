import QtQuick

Item {
    id: root
    property string kind: "list"
    property color ink: "#f7f7f5"

    Repeater {
        visible: root.kind === "list"
        model: 3
        delegate: Row {
            required property int index
            x: 2
            y: 2 + index * 5
            spacing: 3
            Rectangle { width: 2; height: 2; radius: 1; color: root.ink }
            Rectangle { width: 10; height: 2; radius: 1; color: root.ink }
        }
    }

    Row {
        visible: root.kind === "strip"
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: 3
            delegate: Rectangle {
                width: 4
                height: 12
                radius: 1
                color: "transparent"
                border.width: 1
                border.color: root.ink
            }
        }
    }

    Grid {
        visible: root.kind === "grid"
        anchors.centerIn: parent
        columns: 2
        spacing: 2
        Repeater {
            model: 4
            delegate: Rectangle {
                width: 5
                height: 5
                radius: 1
                color: "transparent"
                border.width: 1
                border.color: root.ink
            }
        }
    }

    Item {
        visible: root.kind === "shuffle"
        anchors.fill: parent
        Rectangle { x: 2; y: 5; width: 13; height: 1.5; radius: 1; color: root.ink; rotation: -24 }
        Rectangle { x: 2; y: 10; width: 13; height: 1.5; radius: 1; color: root.ink; rotation: 24 }
        Text {
            x: 12; y: -1
            text: "›"
            color: root.ink
            font.pixelSize: 10
            rotation: -20
        }
        Text {
            x: 12; y: 8
            text: "›"
            color: root.ink
            font.pixelSize: 10
            rotation: 20
        }
    }
}

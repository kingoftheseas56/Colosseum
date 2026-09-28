import QtQuick
import ".."

Rectangle {
    id: pill
    property string label: ""
    property bool selected: false
    property bool interactive: false
    property color textColor: theme.inkDim
    signal triggered()

    implicitWidth: text.implicitWidth + 24
    implicitHeight: 28
    radius: height / 2
    color: selected ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(0.024, 0.027, 0.043, 0.85)
    border.width: 1
    border.color: selected ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.12)
    scale: action.interactionActive && interactive ? 1.04 : 1.0

    Behavior on scale { NumberAnimation { duration: 150 } }
    Behavior on color { ColorAnimation { duration: 150 } }

    Theme { id: theme }

    Text {
        id: text
        anchors.centerIn: parent
        text: pill.label
        color: pill.selected ? "#17181c" : pill.textColor
        font.family: theme.ui
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    KeyboardAction {
        id: action
        anchors.fill: parent
        enabled: pill.interactive
        accessibleName: pill.label
        focusRadius: pill.radius
        onTriggered: pill.triggered()
    }
}

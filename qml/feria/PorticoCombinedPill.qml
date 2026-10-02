import QtQuick
import ".." as Colosseum

Item {
    id: root
    property real unit: 16
    property string label: ""
    property bool selected: false
    property bool focusedState: false
    property color ink: "#f7f7f5"
    property color mist: "#c9c8d0"
    property color gold: "#f0c44a"
    property bool compact: false
    signal triggered()
    signal entered()

    implicitHeight: (compact ? 2.1 : 2.625) * unit
    implicitWidth: labelText.implicitWidth + (compact ? 1.8 : 2.75) * unit

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.selected ? Qt.rgba(1,1,1,0.14) : "transparent"
        border.width: root.compact ? 1 : 0
        border.color: root.compact ? Qt.rgba(1,1,1,0.12) : "transparent"
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: -0.1875 * unit
        radius: height / 2 + 0.1875 * unit
        color: "transparent"
        border.width: 0.1875 * unit
        border.color: root.focusedState ? root.gold : "transparent"
        visible: root.focusedState
    }
    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.label
        color: root.selected ? root.gold : root.mist
        font.family: controller.uiFont
        font.pixelSize: (root.compact ? 0.85 : 1.125) * root.unit
        font.weight: root.selected ? Font.Medium : Font.Normal
    }
    Colosseum.KeyboardAction {
        id: input
        anchors.fill: parent
        focusEnabled: false
        showFocusFrame: false
        accessibleName: root.label
        onTriggered: root.triggered()
    }
    Connections {
        target: input
        function onHoveredChanged() { if (input.hovered) root.entered() }
    }
}

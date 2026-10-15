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

    Colosseum.Theme { id: theme }

    implicitHeight: (compact ? 2.1 : 2.625) * unit
    implicitWidth: labelText.implicitWidth + (compact ? 1.8 : 2.75) * unit

    Rectangle {
        anchors.fill: parent
        radius: root.compact ? 11 : 14
        color: root.selected ? root.gold : "transparent"
        border.width: root.compact ? 1 : 0
        border.color: root.compact ? Qt.rgba(1,1,1,0.12) : "transparent"
    }
    Colosseum.FocusRing {
        anchors.fill: parent
        radius: root.compact ? 11 : 14
        shown: root.focusedState
    }
    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.label
        color: root.selected ? "#1a1408" : root.mist
        font.family: theme.ui
        font.pixelSize: root.compact ? 13 : 14
        font.weight: Font.DemiBold
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

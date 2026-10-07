import QtQuick
import ".."
KeyboardAction {
    id: root
    property string label: ""
    property bool selected: false
    property Item backdrop: null
    implicitWidth: caption.implicitWidth + 28
    implicitHeight: 38
    accessibleName: label
    Theme { id: theme }
    Glass {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: 12
        tint: root.selected ? 0.16 : 0.06
        scrim: 0.18
    }
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: root.selected ? theme.gold : "transparent"
        border.width: root.interactionActive ? 1 : 0
        border.color: theme.gold
    }
    Text {
        id: caption
        anchors.centerIn: parent
        text: root.label
        color: root.selected ? theme.biblioWashBottom : theme.ink
        opacity: root.enabled ? 1 : 0.35
        font.family: theme.ui
        font.pixelSize: 13
    }
}

import QtQuick
import ".."
KeyboardAction {
    focusOnPointer: false
    id: root
    property string label: ""
    property string symbol: ""
    property bool selected: false
    property Item backdrop: null
    focusRadius: 12
    focusPrimary: selected
    implicitWidth: symbol.length > 0 ? 38 : caption.implicitWidth + 28
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
        color: root.selected ? theme.gold : root.interactionActive ? theme.glassTint : "transparent"
    }
    Text {
        id: caption
        anchors.centerIn: parent
        text: root.label
        visible: root.symbol.length === 0
        color: root.selected ? theme.biblioWashBottom : theme.ink
        opacity: root.enabled ? 1 : 0.35
        font.family: theme.ui
        font.pixelSize: 13
        font.weight: root.selected ? Font.DemiBold : Font.Normal
    }
    VinylControlIcon {
        id: controlIcon
        anchors.centerIn: parent
        symbol: root.symbol
        color: root.selected ? theme.biblioWashBottom : theme.ink
        visible: root.symbol.length > 0
        opacity: root.enabled ? 1 : 0.35
    }
}

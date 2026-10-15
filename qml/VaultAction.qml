import QtQuick

Rectangle {
    id: control
    property string text: ""
    property string accessibleName: text
    property bool primary: false
    property bool quiet: false
    property bool selected: false
    property Item nextFocus: null
    property Item previousFocus: null
    property alias keyboardAction: action
    signal triggered()
    implicitWidth: label.implicitWidth + 28
    implicitHeight: 36
    radius: 11
    color: primary ? (action.hovered ? Qt.lighter(skin.gold, 1.08) : skin.gold)
        : selected ? Qt.rgba(skin.gold.r, skin.gold.g, skin.gold.b, 0.14) : action.interactionActive ? skin.glassHi : quiet ? "transparent" : skin.glassTint
    border.width: quiet ? 0 : 1
    border.color: selected ? skin.gold : skin.edge
    opacity: enabled ? 1 : 0.45
    VaultTheme { id: skin }
    Text {
        id: label
        anchors.centerIn: parent
        width: Math.max(0, control.width - (control.text.length <= 2 ? 8 : 28))
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: control.text
        color: control.primary ? "#211a10" : control.selected ? skin.gold : skin.ink
        font.family: skin.ui
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    KeyboardAction {
        id: action
        anchors.fill: parent
        accessibleName: control.accessibleName
        focusRadius: control.radius
        spaceActivates: true
        KeyNavigation.tab: control.nextFocus
        KeyNavigation.backtab: control.previousFocus
        onTriggered: control.triggered()
    }
}

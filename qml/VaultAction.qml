import QtQuick

Rectangle {
    id: control
    property string text: ""
    property string accessibleName: text
    property string tooltipText: ""
    property bool primary: false
    property bool quiet: false
    property bool selected: false
    property Item nextFocus: null
    property Item previousFocus: null
    property alias keyboardAction: action
    // The legacy poster/list glyphs are currently desktop-only; if this shared
    // selector is instantiated on Android, reserve real room for its caption.
    readonly property bool androidCaptionedSelector: Qt.platform.os === "android"
        && (control.text === "▦" || control.text === "☷")
    signal triggered()
    implicitWidth: control.androidCaptionedSelector
        ? Math.max(label.implicitWidth + 28, 116) : label.implicitWidth + 28
    implicitHeight: control.androidCaptionedSelector ? 58 : 36
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
        anchors.verticalCenterOffset: control.androidCaptionedSelector ? -11 : 0
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
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: control.androidCaptionedSelector ? 36 : parent.height
        accessibleName: control.accessibleName
        tooltipText: control.tooltipText
        androidTooltipPolicy: control.androidCaptionedSelector ? "caption" : "drop"
        androidCaptionText: control.accessibleName
        androidCaptionPlacement: "below"
        focusRadius: control.radius
        spaceActivates: true
        KeyNavigation.tab: control.nextFocus
        KeyNavigation.backtab: control.previousFocus
        onTriggered: control.triggered()
    }
}

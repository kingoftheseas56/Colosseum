import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property bool destructive: false
    property bool tabNavigationExit: false
    signal keyboardFocusReceived()
    signal tabNavigationRequested(int direction)
    Theme { id: theme }
    implicitWidth: label.implicitWidth + 28
    implicitHeight: 40
    padding: 12
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        id: label
        text: control.text
        color: control.destructive ? "#ffb4ab" : theme.ink
        font.family: theme.ui
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 10
        color: control.down ? theme.glassHi : control.hovered ? theme.glassTint : "#66141820"
        border.width: 1
        border.color: control.activeFocus ? theme.gold : theme.edge
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    onActiveFocusChanged: if (activeFocus) control.keyboardFocusReceived()
    Keys.onPressed: (event) => {
        if (control.tabNavigationExit && event.key === Qt.Key_Tab
                && (event.modifiers & Qt.ShiftModifier) === 0) {
            control.tabNavigationRequested(1)
            event.accepted = true
        }
    }
}

import QtQuick
import ".."

KeyboardAction {
    id: root
    tooltipText: accessibleName
    property string symbol: "check"
    property bool revealed: false
    focusOnPointer: false
    focusRadius: height / 2
    width: 30
    height: 30
    opacity: revealed ? 1 : 0
    pointerEnabled: revealed
    Behavior on opacity {
        NumberAnimation {
            duration: 120
        }
    }
    Theme {
        id: theme
    }
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(0.025, 0.025, 0.035, root.interactionActive ? 0.8 : 0.58)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.interactionActive ? 0.2 : 0.08)
    }
    VinylControlIcon {
        anchors.centerIn: parent
        width: 14
        height: 14
        symbol: root.symbol
        color: root.interactionActive ? theme.gold : theme.ink
    }
}

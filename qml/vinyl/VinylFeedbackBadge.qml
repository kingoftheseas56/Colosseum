import QtQuick
import ".."

KeyboardAction {
    id: root
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
    // Keep the semantic action in the focus order, but only build its SVG
    // and face when revealed. Keep them alive until the fade-out finishes.
    Loader {
        anchors.fill: parent
        active: root.revealed || root.opacity > 0
        sourceComponent: Rectangle {
            radius: height / 2
            color: Qt.rgba(0.025, 0.025, 0.035, root.interactionActive ? 0.8 : 0.58)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, root.interactionActive ? 0.2 : 0.08)
            Theme { id: theme }
            VinylControlIcon {
                anchors.centerIn: parent
                width: 14
                height: 14
                symbol: root.symbol
                color: root.interactionActive ? theme.gold : theme.ink
            }
        }
    }
}

// HouseScrollBar - the one scrollbar for the whole app. Attach:
// `ScrollBar.vertical: HouseScrollBar { flick: parent }`.
// Overlay (no layout width), gold while active, hidden/faint idle, hidden when content fits.
import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar

    property Flickable flick: null

    orientation: Qt.Vertical
    policy: (flick && flick.contentHeight > flick.height) ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    anchors.rightMargin: 4
    width: 10

    Theme {
        id: theme
    }

    contentItem: Rectangle {
        implicitWidth: 3
        radius: 2
        color: bar.active ? theme.gold : Qt.rgba(1, 1, 1, 0.22)
        opacity: bar.active ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: 400
            }
        }
    }

    background: Rectangle {
        color: "transparent"
    }
}

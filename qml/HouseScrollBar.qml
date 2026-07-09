// HouseScrollBar - the one scrollbar for the whole app. Attach:
// `ScrollBar.vertical: HouseScrollBar { flick: parent }`.
// Overlay (no layout width), flush to the edge, revealed only when the mouse enters the edge zone.
import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar

    property Flickable flick: null
    property bool revealed: edgeHover.hovered || bar.pressed

    orientation: Qt.Vertical
    policy: (flick && flick.contentHeight > flick.height) ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    anchors.rightMargin: 0
    width: 18
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    HoverHandler {
        id: edgeHover
        acceptedDevices: PointerDevice.Mouse
    }

    Theme {
        id: theme
    }

    contentItem: Item {
        implicitWidth: 18

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 3
            radius: 2
            color: bar.pressed ? theme.gold : Qt.rgba(1, 1, 1, 0.34)
            opacity: bar.revealed ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation {
                    duration: 180
                }
            }
        }
    }

    background: Item {
        implicitWidth: 0
        implicitHeight: 0
    }
}

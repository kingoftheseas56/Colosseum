// HouseScrollBar — the app's ONE scrollbar. History, honestly: 2026-07-12 the white hover
// thumb was pulled for a motion-revealed gold sliver; 2026-07-13 Hemanth reversed it on the
// Hall walk ("the gold sliver is not even present… the series view has a proper scrollbar
// that needs to be universal") — the sliver flickered in and out of existence and read as
// NO scrollbar at all. The standard is now the PROPER THUMB: always present when the page
// overflows, grabbable, click-track paging — on every page except the video player and the
// two readers. One file feeds all `ScrollBar.vertical: HouseScrollBar { flick: … }` sites.
import QtQuick
import QtQuick.Controls.Basic

ScrollBar {
    id: bar

    property Flickable flick: null
    property bool prominent: false
    property bool glassStyle: false
    readonly property bool alongShelf: orientation === Qt.Horizontal

    orientation: Qt.Vertical
    // present whenever there's actually something to scroll — never a phantom bar
    policy: flick && (alongShelf ? flick.contentWidth > flick.width + 0.5
                                : flick.contentHeight > flick.height)
            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    interactive: true                     // grabbable thumb; the track pages on click
    anchors.left: flick && alongShelf ? flick.left : undefined
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick && !alongShelf ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    anchors.rightMargin: 0
    width: alongShelf && flick ? flick.width : prominent && !glassStyle ? 14 : 12
    height: alongShelf ? 12 : flick ? flick.height : implicitHeight
    padding: glassStyle ? 3 : 2



    // the track — a whisper of a rail so the thumb has a lane to live in
    background: Rectangle {
        implicitWidth: bar.prominent ? 14 : 12
        radius: bar.glassStyle ? 6 : 0
        color: Qt.rgba(0.97, 0.97, 0.96,
                       bar.glassStyle ? (bar.hovered || bar.pressed ? 0.09 : 0.035)
                       : bar.prominent ? (bar.hovered || bar.pressed ? 0.16 : 0.10)
                                     : (bar.hovered || bar.pressed ? 0.06 : 0.03))
        visible: bar.policy !== ScrollBar.AlwaysOff
        Behavior on color { ColorAnimation { duration: 140 } }
    }

    // the thumb — the proper handle (the series-view look, ratified 2026-07-13)
    contentItem: Rectangle {
        implicitWidth: bar.prominent ? 10 : 8
        radius: 4
        border.width: bar.glassStyle ? 1 : 0
        border.color: Qt.rgba(0.96, 0.98, 1, bar.hovered || bar.pressed ? 0.25 : 0.12)
        color: bar.glassStyle ? Qt.rgba(0.89, 0.93, 1, bar.pressed ? 0.58 : bar.hovered ? 0.40 : 0.23)
             : bar.pressed ? Qt.rgba(0.94, 0.77, 0.29, 0.85)          // gold under the hand
             : bar.hovered ? Qt.rgba(0.97, 0.97, 0.96, bar.prominent ? 0.82 : 0.55)
                           : Qt.rgba(0.97, 0.97, 0.96, bar.prominent ? 0.62 : 0.30)
        visible: bar.policy !== ScrollBar.AlwaysOff
        Behavior on color { ColorAnimation { duration: 140 } }
    }
}


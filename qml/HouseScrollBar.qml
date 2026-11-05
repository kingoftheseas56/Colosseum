// HouseScrollBar — the app's ONE scrollbar. History, honestly: 2026-07-12 the white hover
// thumb was pulled for a motion-revealed gold sliver; 2026-07-13 Hemanth reversed it on the
// Hall walk ("the gold sliver is not even present… the series view has a proper scrollbar
// that needs to be universal") — the sliver flickered in and out of existence and read as
// NO scrollbar at all. The standard is now the PROPER THUMB: always present when the page
// overflows, grabbable, click-track paging — on every page except the video player and the
// two readers. One file feeds all `ScrollBar.vertical: HouseScrollBar { flick: … }` sites.
import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar

    property Flickable flick: null

    orientation: Qt.Vertical
    // present whenever there's actually something to scroll — never a phantom bar
    policy: (flick && flick.contentHeight > flick.height) ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    interactive: true                     // grabbable thumb; the track pages on click
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    anchors.rightMargin: 0
    width: 12

    Theme { id: theme }

    // the track — a whisper of a rail so the thumb has a lane to live in
    background: Rectangle {
        implicitWidth: 12
        color: Qt.rgba(0.97, 0.97, 0.96, bar.hovered || bar.pressed ? 0.06 : 0.03)
        visible: bar.policy !== ScrollBar.AlwaysOff
        Behavior on color { ColorAnimation { duration: 140 } }
    }

    // the thumb — the proper handle (the series-view look, ratified 2026-07-13)
    contentItem: Rectangle {
        implicitWidth: 8
        radius: 4
        color: bar.pressed ? Qt.rgba(0.94, 0.77, 0.29, 0.85)          // gold under the hand
             : bar.hovered ? Qt.rgba(0.97, 0.97, 0.96, 0.55)
                           : Qt.rgba(0.97, 0.97, 0.96, 0.30)
        visible: bar.policy !== ScrollBar.AlwaysOff
        Behavior on color { ColorAnimation { duration: 140 } }
    }
}

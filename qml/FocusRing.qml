// FocusRing — Colosseum's one focus look (world-feel spec R1): a solid 3 px gold ring drawn
// 3 px outside the focused thing, so wherever focus is you can see it. Every focus visual in
// the app draws this instead of its own halo, frame or tint.
//
// Fill it over the focused item: `FocusRing { anchors.fill: target; shown: …; radius: … }`.
// `radius` is the TARGET's corner radius; the ring adds its own outset. `primary` is for
// gold buttons (Watch / Read), where a gold ring would vanish: a dark gap separates the two.
import QtQuick

Item {
    id: ring

    property bool shown: false
    property real radius: 10
    property bool primary: false
    readonly property real outset: ring.primary ? 6 : 3

    visible: ring.shown
    z: 10000

    // the dark separation band between a gold button and its gold ring
    Rectangle {
        visible: ring.primary
        anchors.fill: parent
        anchors.margins: -3
        radius: ring.radius + 3
        color: "transparent"
        border.width: 3
        border.color: "#06070b"
    }

    Rectangle {
        objectName: "focusRingStroke"
        anchors.fill: parent
        anchors.margins: -ring.outset
        radius: ring.radius + ring.outset
        color: "transparent"
        border.width: 3
        border.color: "#f0c44a"
    }
}

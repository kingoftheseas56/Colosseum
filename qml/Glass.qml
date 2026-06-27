// Glass — the Colosseum material. A frosted surface with REAL backdrop blur of whatever
// sits in `backdrop` behind it. Proven in the spine slice; reused for all chrome.
//
// Coordinate note: the blurred region is mapped from this item's position INTO the backdrop's
// coordinate space, so a Glass placed anywhere (not just a direct child) blurs the right patch.
// (Live-tracking blur during vertical SCROLL is a known later problem — v1 home is single-screen.)

import QtQuick
import QtQuick.Effects

Item {
    id: root

    // what to blur (the persistent wallpaper / content layer)
    required property Item backdrop
    property real radius: 18
    property real tint: 0.10          // glass white film
    property real scrim: 0.14         // adaptive scrim: keeps text legible over ANY wallpaper
    property real blurAmount: 1.0
    property color edge: Qt.rgba(1, 1, 1, 0.18)

    // Bind to a scroll offset (e.g. flick.contentY) when this surface lives inside a scroller,
    // so the blurred backdrop region recomputes as the panel moves. Static surfaces leave it 0.
    property real track: 0

    default property alias content: holder.data

    // this panel's top-left expressed in backdrop coordinates (reactive to x/y AND scroll)
    readonly property point _origin: {
        root.track;   // dependency: re-evaluate when the bound scroll offset changes
        return root.backdrop ? root.mapToItem(root.backdrop, 0, 0) : Qt.point(0, 0);
    }

    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        visible: false
        live: true
        hideSource: false
        sourceItem: root.backdrop
        sourceRect: Qt.rect(root._origin.x, root._origin.y, root.width, root.height)
    }
    Item {
        id: maskItem
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: root.radius; color: "white" }
    }
    MultiEffect {
        anchors.fill: parent
        source: grab
        autoPaddingEnabled: false
        blurEnabled: true
        blur: root.blurAmount
        blurMax: 48
        maskEnabled: true
        maskSource: maskItem
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(1, 1, 1, root.tint)
        border.width: 1
        border.color: root.edge
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(0, 0, 0, root.scrim)
    }
    Item { id: holder; anchors.fill: parent }
}

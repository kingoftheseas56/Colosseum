// HouseScrollBar — the app's single scroll indicator. Hemanth 2026-07-12: the old white
// hover thumb read as an ugly slab and was pulled; this is its replacement — a barely-there
// GOLD sliver that fades in ONLY while you're actively scrolling (flick / drag / wheel
// motion) and fades back out, with a gentle linger, once the page settles. Thin, edge-flush,
// non-interactive (pure indicator — pages scroll by wheel/drag). One file feeds all ~27
// `ScrollBar.vertical: HouseScrollBar { flick: … }` attach sites.
import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar

    property Flickable flick: null

    orientation: Qt.Vertical
    // present only when there's actually something to scroll
    policy: (flick && flick.contentHeight > flick.height) ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    interactive: false                    // indicator only: never a hit target on the right edge
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    anchors.rightMargin: 0
    width: 6

    Theme { id: theme }

    // "actively scrolling" = the flickable is moving under a flick, drag, or wheel tick.
    // 2026-07-13 (Hemanth, Hall of Worlds: "still does not have a scroll bar"): ScrollGlide
    // animates contentY PROGRAMMATICALLY — that never sets moving/flicking, so wheel scrolls
    // showed NO sliver anywhere. The reveal now also rides contentY motion itself.
    property bool _contentMoving: false
    Timer { id: settleTimer; interval: 500; onTriggered: bar._contentMoving = false }
    Connections {
        target: bar.flick
        function onContentYChanged() { bar._contentMoving = true; settleTimer.restart() }
    }
    readonly property bool scrolling: !!flick && (flick.moving || flick.flicking || _contentMoving)

    // kill the styled defaults so ONLY our sliver draws
    contentItem: Item { implicitWidth: 0; implicitHeight: 0 }
    background: Rectangle { implicitWidth: 0; implicitHeight: 0; color: "transparent"; visible: false }

    Rectangle {
        id: thumb
        anchors.right: parent.right
        width: 3
        radius: 1.5
        color: theme.gold
        height: Math.max(28, bar.visualSize * bar.height)
        y: Math.max(0, Math.min(bar.height - height, bar.visualPosition * bar.height))
        opacity: bar.scrolling ? 0.85 : 0.0
        visible: bar.policy !== ScrollBar.AlwaysOff

        // fade in quick while scrolling; fade out slow so it lingers a beat after you stop
        Behavior on opacity {
            NumberAnimation { duration: bar.scrolling ? 120 : 450; easing.type: Easing.OutCubic }
        }
    }
}

// ComicReaderUnitPlaceholder — the quiet stand-in a paged unit shows before it has pixels.
//
// WHY THIS EXISTS AT ALL (justified up from zero, Task 4 / overhaul plan 2026-07-28):
// A paged surface that will not paint a half-decoded unit has to draw SOMETHING in the meantime, and
// the alternative to this file is inlining the same eight lines into both paged surfaces — where the
// two copies then drift, which is exactly how a reader ends up with two different "loading" looks.
// One leaf visual with two consumers (Single and Pair), no behaviour, no state machine.
//
// RESTRAINED is the requirement, in Hemanth's word: a page-shaped panel a shade above the black
// stage, and nothing else. No spinner, no percentage, no pulsing — a reader waiting a beat for a
// page should feel like paper that has not turned yet, not like software thinking.
//
// The FADE is what keeps it from being a flicker, and the EASING is the load-bearing half of that.
// It has to be ease-IN (slow at the start): a page that decodes in 40ms then only reaches t^2 = 0.08
// of full opacity before it is told to go away again, which is imperceptible. An earlier draft used
// OutQuad, which is fastest at the start — exactly backwards for suppressing a flash: at the same
// 40ms it reached t(2-t) = 0.49, so a fast turn showed a ~50% grey panel and faded it back out. (The
// comment there also claimed "about a third", which was wrong about its own curve.) That is the whole
// reason `shown` is a property and `visible` is derived from the animated opacity — the caller states
// intent, the component decides when it is actually worth drawing.
//
// GEOMETRY IS THE CALLER'S: this fills whatever box it is given. The paged surfaces put it exactly
// where the page (or each half of the pair) will land, so the unit does not jump when it arrives.

import QtQuick

Item {
    id: root

    // The caller's intent — "this unit has nothing to show yet". Never bound to `visible` directly:
    // see the fade note above.
    property bool shown: false

    // A shade above the stage's black. Deliberately NOT a mid-grey card: at reading brightness a
    // light panel is a flash in a dark room, and this appears on every page turn that outruns the
    // decode.
    property color panelColor: "#0b0b0e"
    property color edgeColor: "#1b1b21"

    opacity: shown ? 1 : 0
    // InQuad, NOT OutQuad — see the fade note in the header. Slow at the start is what makes a fast
    // decode never show this.
    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.InQuad } }
    // Below 1% it is not on screen in any meaningful sense, so it stops costing a node.
    visible: opacity > 0.01

    Rectangle {
        anchors.fill: parent
        color: root.panelColor
        border.color: root.edgeColor
        border.width: 1
        radius: 2
    }
}

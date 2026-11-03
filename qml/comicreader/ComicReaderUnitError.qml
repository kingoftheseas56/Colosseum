// ComicReaderUnitError — the typed placard a paged surface shows for a page that cannot arrive.
//
// WHY A FILE (justified up from zero, Task 4 / overhaul plan 2026-07-28): the Long Strip surface
// already draws this exact placard per delegate ("typed error placard — this page only",
// ComicReaderStripSurface.qml). Task 4 adds two more places that need it — the Single surface and
// each half of a Pair — and inlining it there would have made three hand-copied versions of one
// visual, which is how a second error language gets invented by accident. So: ONE leaf visual, the
// strip's own colours and wording carried over verbatim, two consumers today.
//
// (The strip's copy is deliberately left alone this task — it lives inside a ListView delegate and
// re-plumbing it is a change to a surface Task 4 has no other reason to touch. Its colours, sizes and
// wording are the ones reproduced here, so for the three real codes the two are identical. They DO
// differ on an unrecognised code, by design: the strip draws no card at all, this draws an honest
// fallback — see the note below. Folding the strip's copy in is a later tidy.)
//
// TYPED, not generic: the codes are the backend's snake_case PageError wire codes
// (ComicReaderTypes.h — none / missing_file / decode_failed / unsupported_image), so the reader is
// told which of the three actually happened. An unrecognised code still says something honest
// rather than rendering an empty card.
//
// TWO WAYS OUT (Task 11, overhaul plan 2026-07-28). Until now this card was a dead end: it named
// the problem and offered nothing. The approved design is "the reader shows a restrained error card
// in that page's place, offers Retry and Skip, and keeps surrounding pages usable", so the card now
// carries exactly those two actions and nothing else.
//
// It RAISES them, it never performs them. Retry is a backend re-read (ComicReaderCore::retryPage)
// and Skip is a navigation, and neither is a placard's business — routing them through the surface
// to the shell is what keeps "Retry never mutates the archive" a property of one tested function
// instead of a promise repeated in three visuals. Both signals carry the page, because in Pair mode
// two of these cards can be on screen at once and the good half must not be the one retried.
//
// THE STRIP'S HAND-COPY IS GONE (Task 11). Task 4 left the Long Strip surface drawing its own inline
// version of this card and called folding it in "a later tidy"; adding actions made it load-bearing
// rather than tidiness, because a Long Strip reader would otherwise have been left with the exact
// dead end this task exists to close. One card, one error language, three mounts.

import QtQuick
import ".."

Item {
    id: root

    // The backend's wire code for this page's failure.
    property string code: ""
    // 0-based page index; -1 omits the caption (the caller does not always know which page it is).
    property int pageIndex: -1
    // Whether to offer the two actions at all. The strip's delegate mounts this card for pages the
    // reader may merely be scrolling PAST, and a column of live buttons flying by is noise — so the
    // mount decides. Default true: a card that names a problem should offer the way out unless its
    // owner has a reason otherwise.
    property bool actionsEnabled: true

    // Raised, never performed. `pageIndex` rides along because a Pair can show two of these.
    signal retryRequested(int page)
    signal skipRequested(int page)

    readonly property string headline: {
        switch (code) {
        case "missing_file":      return "Page missing"
        case "decode_failed":     return "Couldn't decode"
        case "unsupported_image": return "Unsupported format"
        default:                  return "Couldn't load this page"
        }
    }

    // ONE button shape, declared once. An inline component has to be a direct child of the
    // document's root object, so it lives here rather than beside its two uses below.
    component CardAction: Rectangle {
        id: btn
        property string label: ""
        signal activated()
        width: Math.max(76, cap.implicitWidth + 26)
        height: 28
        radius: 6
        color: hover.hovered ? "#241d30" : "#1b1624"
        border.color: hover.hovered ? "#4a3f5e" : "#2f2740"
        border.width: 1
        Text {
            id: cap
            anchors.centerIn: parent
            text: btn.label
            color: "#d8d4e2"
            font.pixelSize: 12
            font.family: "Segoe UI"
            font.weight: Font.DemiBold
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: btn.activated() }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: btn.label
            onTriggered: btn.activated()
        }
    }

    // ---- readbacks for the surfaces gate. They read the ITEMS, never re-derive the rule, so a
    // fixture cannot pass against a card the screen does not draw. `visible` is deliberately NOT
    // what is exposed: QQuickItem.visible reads back EFFECTIVE visibility, so under an offscreen
    // harness (whose tree is rooted invisible) every one of them reads false whatever its binding
    // says, and the assertion would quietly stop testing anything. ----
    readonly property bool actionsShown: actionRow.shown

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(Math.max(1, parent.width) * 0.72, 360)
        // The card grows for the actions rather than the actions crowding the text: 92 is the
        // height Task 4 shipped and the wording sits in it exactly as before.
        height: actionRow.shown ? 130 : 92
        radius: 10
        color: "#141019"
        border.color: "#2a2334"
        border.width: 1

        Column {
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.headline
                color: "#ff8a8a"
                font.pixelSize: 14
                font.family: "Segoe UI"
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.pageIndex >= 0
                text: "Page " + (root.pageIndex + 1)
                color: "#9a99a5"
                font.pixelSize: 11
                font.family: "Segoe UI"
            }
            Row {
                id: actionRow
                // The RULE, as a named property, for the same reason the surfaces expose theirs:
                // `visible` is effective visibility and would read false in any offscreen tree.
                readonly property bool shown: root.actionsEnabled && root.code.length > 0
                visible: shown
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                CardAction {
                    objectName: "retryAction"
                    label: "Retry"
                    onActivated: root.retryRequested(root.pageIndex)
                }
                CardAction {
                    objectName: "skipAction"
                    label: "Skip"
                    onActivated: root.skipRequested(root.pageIndex)
                }
            }
        }
    }
}

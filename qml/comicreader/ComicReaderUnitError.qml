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

import QtQuick

Item {
    id: root

    // The backend's wire code for this page's failure.
    property string code: ""
    // 0-based page index; -1 omits the caption (the caller does not always know which page it is).
    property int pageIndex: -1

    readonly property string headline: {
        switch (code) {
        case "missing_file":      return "Page missing"
        case "decode_failed":     return "Couldn't decode"
        case "unsupported_image": return "Unsupported format"
        default:                  return "Couldn't load this page"
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(Math.max(1, parent.width) * 0.72, 360)
        height: 92
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
        }
    }
}

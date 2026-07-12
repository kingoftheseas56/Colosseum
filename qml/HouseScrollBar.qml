// HouseScrollBar — the app's single scroll component. Hemanth 2026-07-12: the hover-
// revealed thumb read as an "ugly white bar" on the right edge of every page (worst on
// low-overflow pages, where the thumb sizes tall and the translucent white reads as a
// slab) — REMOVED app-wide. Pages scroll by wheel/drag (they're Flickables); this stays
// as an inert, no-draw ScrollBar so the ~27 `ScrollBar.vertical: HouseScrollBar { flick: … }`
// attach sites keep compiling, and a scroll indicator can be restored in THIS one file if
// ever wanted.
import QtQuick
import QtQuick.Controls

ScrollBar {
    property Flickable flick: null       // kept for attach-site compatibility (unused now)

    orientation: Qt.Vertical
    policy: ScrollBar.AlwaysOff          // never shown — no bar, no rail, no hit target
    interactive: false

    // draw nothing: no thumb, no track, no background
    contentItem: Item { implicitWidth: 0; implicitHeight: 0 }
    background: Item { implicitWidth: 0; implicitHeight: 0 }
}

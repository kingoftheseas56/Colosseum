// ComicReaderLoupe — the temporary full-resolution magnifier (Task 9, plan 2026-07-28).
//
// This COMPLETES a scaffold rather than adding a feature. The reader already had the Loupe command,
// the `L` shortcut, the `search` glyph, `loupeRequested()` and tests proving the request fires —
// and nothing at all consumed it. Hemanth's ruling when he noticed: "So we'll complete the existing
// Loupe using YACReader's useful behavior, not add a duplicate feature." (YACReader is a strict
// BEHAVIOURAL reference — GPL-3.0 — never a source of code.)
//
// The approved shape, verbatim from the design ledger:
//
//   "circular lens following the pointer, 2.0x default magnification, adjustable 1.5x-4.0x by wheel
//    or +/-, full-resolution cached page sampling, click to pin; click again to resume following,
//    flips inward near viewport edges, works in Single, Pair, and Long Strip, pauses Auto-scroll
//    while active, never changes page zoom, pan, layout, or reading position, closes through Loupe,
//    L, Escape, or its close action."
//
// THE LINE THAT GOVERNS THIS FILE is the second to last: *never changes page zoom, pan, layout, or
// reading position*. This is a temporary INSPECTION tool, not a fourth way to zoom. If using it
// moves the book, it is wrong. That rule is kept STRUCTURALLY rather than by guard:
//
//   * this component owns no reading state — the pages it draws are pushed in as plain boxes;
//   * it raises exactly ONE intent, dismissRequested(), which carries nothing;
//   * it never touches `core`, a surface, a page number, a zoom, a scroll position or a record —
//     it has no reference to any of them, so there is no code path to audit.
//
// HOW IT SAMPLES (the part that is easy to get wrong, and the reason the design says "full-
// resolution cached page sampling" rather than just "magnifier"). The cheap route is a
// ShaderEffectSource over the live page item — but that samples the already-downscaled ON-SCREEN
// texture, so magnifying it shows you bigger blurry pixels, which defeats the entire point of a
// loupe on a 2400px scan. So the lens re-requests the page through the SAME provider the reading
// surface uses, at its OWN request size:
//
//     sourceSize.width = drawnWidth x magnificationMax          (the TOP magnification, 4.0)
//
// Task 2's provider treats the requested width as a CEILING it may downscale TO, never a size it
// upscales to (ComicReaderImageResponse: `scaling = targetWidth > 0 && targetWidth < source.width`).
// So the lens gets, from the decoded SOURCE page:
//   * the untouched full-resolution page whenever 4x the drawn size is at or beyond the scan's own
//     resolution — the normal case for a real comic scan at any sane window size; or
//   * an exact SmoothTransformation scale of that full-resolution page down to 4x the drawn size,
//     which is still at or above every pixel the lens ever displays.
// Either way the lens is looking at the decoded page, never at the screen. It is pinned at the TOP
// magnification rather than the live one on purpose: a request width that moved with the wheel
// would re-scale the page on every notch, and seat a new scaled-cache entry per notch.
//
// It costs the reader nothing it was not already paying: the page under the lens is on screen, so
// ComicReaderCore::setVisible has already PINNED it in the source page cache (visible pages plus
// their neighbours never evict). The lens asks for a page that is already resident, and it never
// calls setVisible itself — the page behind the lens is never blanked to feed the lens.
//
// WHAT IT INHERITS. The url is the reader's own imageUrl(), so the sample rides the whole render
// path: the Image panel's brightness / contrast / gamma / sharpen / rotation / auto-crop are all
// applied by the provider before the pixels come back, and the ?rev= in the url self-busts when
// they change. The one thing the provider does NOT own is the night veil — that is a composited
// black Rectangle the shell paints over the surfaces, so the lens paints its own at the same
// opacity (`veilOpacity`). Without it the lens would glare bright white out of a dimmed page.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome — ComicReaderPagesOverlay,
// ComicReaderImagePopover and ComicReaderLayoutPopover are this component's siblings and the four
// are deliberately one family: `open` is a RULE-level property (QQuickItem.visible is EFFECTIVE
// visibility, so a test asserting on a child's `visible` reads its ancestors' state too), every
// control is a plain harness-callable function that the pointer paths then call, and dismissal is
// one signal the shell acts on.

import QtQuick
import QtQuick.Effects
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    objectName: "readerLoupe"

    // Fill the shell explicitly. anchors.fill resolves to 0x0 for this overlay mount (the same QML
    // quirk the settings sheet, the filmstrip and both popovers document); an explicit size binding
    // tracks the parent reliably and follows a resize / fullscreen flip.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= injected facts (never written back) =================
    // The pages the reader is DRAWING right now, each with the box it occupies in THIS item's
    // coordinates: [{ page (0-based), url, x, y, width, height }]. The shell reads them off whichever
    // surface is live, so ONE shape answers for Single, Pair and Long Strip — a pair is two entries
    // (which is what lets the lens straddle the gutter), a strip is however many rows are on screen,
    // and Single is one. This component never asks a surface anything.
    property var pages: []
    // The reader's night veil, mirrored so the lens dims exactly as the page under it does.
    property real veilOpacity: 0

    // ================= the chrome this surface has to live between =================
    // Mirrors ComicReaderHud's own numbers: the title strip (38) + the command bar (46) at the top,
    // the gold rail (54) at the bottom. Properties rather than literals so a shell that ever changes
    // the chrome says so in one place.
    //
    // The POINTER TRACKER is inset by these, and that is load-bearing rather than tidy: while the
    // Loupe is open the reader is modal, so the chrome is pinned awake (ComicReaderHud._holdChrome)
    // and those two bands are covered by chrome anyway — there is no comic under them to inspect.
    // Leaving them clear is what keeps the Loupe command itself, Back and the gold rail clickable,
    // which is what makes "closes through Loupe" true rather than merely claimed. The LENS is not
    // inset: it is a glass you are holding over the page, and it occludes what it is over.
    property int chromeTopInset: 84
    property int railHeight: 54
    property int edgeMargin: 8

    // ---- open state: the RULE, not the pixels (see the header note) ----
    property bool open: false
    visible: open

    // ================= the ONE intent =================
    signal dismissRequested()          // close me; nothing about the book changes

    Theme { id: theme }

    readonly property color cRim:      Qt.rgba(1, 1, 1, 0.55)
    readonly property color cRimInner: Qt.rgba(0, 0, 0, 0.55)
    readonly property color cChipBg:   Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.94)
    readonly property color cChipEdge: Qt.rgba(1, 1, 1, 0.16)

    // ================= the magnification contract, in one place =================
    readonly property real magnificationMin: 1.5
    readonly property real magnificationMax: 4.0
    readonly property real magnificationDefault: 2.0
    readonly property real magnificationStep: 0.25
    property real magnification: magnificationDefault

    // ================= the lens =================
    property int lensSize: 224
    readonly property real lensRadius: lensSize / 2

    // THE ANCHOR: the point on the reader you asked to inspect. `pinAt` freezes it, `followPointer`
    // moves it, and the edge flip below deliberately does NOT touch it — see lensCenterX.
    property real lensX: 0
    property real lensY: 0
    property bool pinned: false

    // ---- the edge flip ----
    // Near a viewport edge the lens BODY moves inward so it stays fully on screen; the ANCHOR stays
    // exactly where you pointed. That asymmetry is the whole design: the content is magnified ABOUT
    // the anchor (see magnifiedRect), so the pixel under your pointer stays under your pointer even
    // when the glass around it has slid inward. Recentring the sample on the moved lens instead
    // would show you something you did not point at, which is the one thing a loupe may not do.
    //
    // In a viewport narrower than the lens the clamp has no solution and Math.max wins — the lens
    // then hangs off the right/bottom rather than jittering. A reader that small has no page to
    // inspect either, so this is a floor, not a route.
    readonly property real lensCenterX:
        Math.max(lensRadius + edgeMargin, Math.min(width - lensRadius - edgeMargin, lensX))
    readonly property real lensCenterY:
        Math.max(lensRadius + edgeMargin, Math.min(height - lensRadius - edgeMargin, lensY))
    readonly property real lensLeft:   lensCenterX - lensRadius
    readonly property real lensTop:    lensCenterY - lensRadius
    readonly property real lensRight:  lensCenterX + lensRadius
    readonly property real lensBottom: lensCenterY + lensRadius
    // Did the flip actually move anything, and did it work? Named rather than re-derived, so the
    // gate asserts the rule the drawing uses instead of a copy of its arithmetic.
    readonly property bool edgeFlipped:
        Math.abs(lensCenterX - lensX) > 0.01 || Math.abs(lensCenterY - lensY) > 0.01
    readonly property bool lensFullyVisible:
        lensLeft >= -0.01 && lensRight <= width + 0.01 && lensTop >= -0.01 && lensBottom <= height + 0.01

    // ================= the named verbs (the pointer paths call THESE; so do the gates) =================

    // Magnification, clamped at this component's own door. A control that can emit 9 and be silently
    // corrected is a control whose readout lies about what it just asked for.
    function setMagnification(value) {
        var m = Number(value)
        if (!isFinite(m)) return
        magnification = Math.max(magnificationMin, Math.min(magnificationMax, m))
    }
    // The wheel and +/- are the SAME adjustment, expressed in steps, so the two doors the design
    // names can never disagree about how big a nudge is.
    function magnifySteps(steps) {
        var n = Number(steps)
        if (!isFinite(n)) return
        setMagnification(magnification + n * magnificationStep)
    }

    // THE pointer door. A pinned lens ignores the pointer entirely — that IS pinning.
    function followPointer(x, y) {
        if (pinned) return
        var nx = Number(x), ny = Number(y)
        if (!isFinite(nx) || !isFinite(ny)) return
        lensX = nx; lensY = ny
    }
    function pinAt(x, y) {
        var nx = Number(x), ny = Number(y)
        if (!isFinite(nx) || !isFinite(ny)) return
        lensX = nx; lensY = ny; pinned = true
    }
    function unpin() { pinned = false }
    // "click to pin; click again to resume following". Releasing resumes FROM the click point rather
    // than snapping back to wherever the pin was — the pointer is here now, and the lens jumping
    // away to its old spot for one frame before the next move caught up read as a glitch.
    function clickAt(x, y) {
        if (pinned) { pinned = false; lensX = Number(x); lensY = Number(y) }
        else pinAt(x, y)
    }
    // Close, and ONLY close.
    function dismiss() { dismissRequested() }

    // A fresh lens starts unpinned, over the middle of the comic — not parked at the origin waiting
    // for a first mouse move, which is what a bare default would give on a keyboard-opened Loupe.
    onOpenChanged: {
        if (!open) return
        pinned = false
        lensX = width / 2
        lensY = (chromeTopInset + Math.max(chromeTopInset, height - railHeight)) / 2
    }

    // ================= pure geometry (the drawn lens binds to THESE; so do the gates) =================

    // Which drawn page is under a point in this item's coordinates, or -1 for the gutter, the
    // letterbox, or anywhere else that is not paper.
    function pageAt(x, y) {
        for (var i = 0; i < pages.length; i++) {
            var p = pages[i]
            if (!p || !(p.width > 0) || !(p.height > 0)) continue
            if (x >= p.x && x < p.x + p.width && y >= p.y && y < p.y + p.height) return p.page
        }
        return -1
    }
    // Where a given page is drawn, or null when it is not on screen at all.
    function pageRect(page) {
        for (var i = 0; i < pages.length; i++) {
            var p = pages[i]
            if (p && p.page === page)
                return { x: p.x, y: p.y, width: p.width, height: p.height }
        }
        return null
    }
    // A drawn box, blown up about the ANCHOR. Scaling about the anchor rather than about the lens
    // centre is what keeps the pixel under the pointer under the pointer through an edge flip.
    function magnifiedRect(p) {
        var m = magnification
        return { x: lensX + (p.x - lensX) * m,
                 y: lensY + (p.y - lensY) * m,
                 width: p.width * m,
                 height: p.height * m }
    }
    function intersectsLens(box) {
        if (!box || !(box.width > 0) || !(box.height > 0)) return false
        return box.x < lensRight && box.x + box.width > lensLeft
            && box.y < lensBottom && box.y + box.height > lensTop
    }
    // The width the lens asks the provider for. Derived from the TOP magnification, so it is at or
    // above every pixel the lens ever displays AND it does not move as the wheel turns. See the
    // header for why that matters to the cache.
    function sampleWidthFor(p) {
        return Math.max(1, Math.round(Number(p.width) * magnificationMax))
    }

    // What the lens is actually showing: every drawn page whose magnified box reaches the glass,
    // with its box already in LENS-LOCAL coordinates. A readback for the gate, computed by the same
    // two functions the delegates below bind to — so the tested arithmetic is the drawn arithmetic.
    // A function rather than a bound property on purpose: bound, it would re-allocate this list on
    // every pointer move whether or not anything read it.
    function sampledPages() {
        var out = []
        for (var i = 0; i < pages.length; i++) {
            var p = pages[i]
            if (!p || !(p.width > 0) || !(p.height > 0)) continue
            var box = magnifiedRect(p)
            if (!intersectsLens(box)) continue
            out.push({ page: p.page,
                       url: (p.url === undefined || p.url === null) ? "" : String(p.url),
                       x: box.x - lensLeft, y: box.y - lensTop,
                       width: box.width, height: box.height,
                       sampleWidth: sampleWidthFor(p) })
        }
        return out
    }

    // The magnification as it reads on the face: "2x", "2.5x", "1.5x".
    function magnificationText(v) {
        var m = Math.round(Number(v) * 100) / 100
        if (!isFinite(m)) m = magnificationDefault
        return (m === Math.round(m) ? String(m) : m.toFixed(2).replace(/0$/, "")) + "×"
    }

    // ================= the pointer tracker =================
    // Covers the COMIC only (see chromeTopInset above). It swallows the wheel so a notch over the
    // lens can never reach the column underneath — the strip surface's own intake is ALSO locked by
    // the shell while the Loupe is open, so the rule holds whichever way Qt happens to route the
    // event, rather than resting on delivery order.
    MouseArea {
        id: tracker
        objectName: "loupeTracker"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.chromeTopInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.railHeight
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        // The tracker's own origin is offset by the chrome inset, so every reading is lifted back
        // into the root's coordinates — the same space `pages` is expressed in.
        function moved(mx, my) { root.followPointer(mx, my + tracker.y) }
        function tap(mx, my)   { root.clickAt(mx, my + tracker.y) }
        onPositionChanged: function (m) { moved(m.x, m.y) }
        onClicked: function (m) { tap(m.x, m.y) }
        onWheel: function (w) {
            root.magnifySteps(w.angleDelta.y >= 0 ? 1 : -1)
            w.accepted = true
        }
    }

    // ================= the lens =================
    Item {
        id: lens
        objectName: "loupeLens"
        x: root.lensLeft
        y: root.lensTop
        width: root.lensSize
        height: root.lensSize
        visible: root.open

        // The circular mask source. Hidden and layered: MultiEffect reads its texture, nothing
        // draws it. (The same shape ContinueTile uses for its rounded art slot.)
        Item {
            id: lensMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; radius: width / 2; color: "white" }
        }

        Item {
            id: lensContent
            anchors.fill: parent
            layer.enabled: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: lensMask }

            // the black stage, under the magnified paper — the same ground the surfaces sit on, so
            // the letterbox inside the lens matches the letterbox outside it
            Rectangle { anchors.fill: parent; color: "#000000" }

            // ONE magnified copy per drawn page. Driven off `pages` (which only changes when the
            // surface's geometry does) rather than off the sample list, so a pointer move
            // re-evaluates two bindings instead of destroying and rebuilding delegates 60 times a
            // second.
            Repeater {
                model: root.pages
                delegate: Image {
                    id: sample
                    required property var modelData
                    readonly property var box: root.magnifiedRect(modelData)
                    visible: root.intersectsLens(box)
                    x: box.x - root.lensLeft
                    y: box.y - root.lensTop
                    width: box.width
                    height: box.height
                    source: (modelData && modelData.url) ? modelData.url : ""
                    asynchronous: true
                    cache: true              // the ?rev= in the url self-busts on a real redecode
                    retainWhileLoading: true // never blank the glass mid-adjustment
                    // The SAME fit the surfaces use, in a box that is their box scaled — so the
                    // magnified page lands exactly over the page it is magnifying, letterbox and
                    // all, instead of drifting by whatever the two fits disagreed about.
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: root.sampleWidthFor(modelData)
                    // NOT mipmapped, deliberately: mipmaps are for a downscale you want smooth, and
                    // this is the one place in the reader where the point is to see the grain.
                    smooth: true
                }
            }

            // the reader's night veil, so the glass dims with the page rather than glaring out of it
            Rectangle {
                objectName: "loupeVeil"
                anchors.fill: parent
                color: "black"
                opacity: root.veilOpacity
                visible: opacity > 0.001
            }
        }

        // the rim — a thin bright ring with a dark inner line, so the glass reads as an object over
        // the page whether the page under it is white paper or a black gutter
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: root.pinned ? theme.gold : root.cRim
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: root.cRimInner
            }
        }
    }

    // ================= the lens's own chip: the readout, the pin state, and the way out =================
    // The design names four ways out — the Loupe command, L, Escape, "or its close action". This is
    // that close action, and it doubles as the readout for a value you otherwise adjust blind.
    Rectangle {
        id: chip
        objectName: "loupeChip"
        visible: root.open
        width: chipRow.implicitWidth + 20
        height: 26
        radius: 13
        color: root.cChipBg
        border.width: 1
        border.color: root.cChipEdge
        // Under the lens where there is room, above it when the lens is near the bottom, and always
        // inside the reader.
        x: Math.max(root.edgeMargin, Math.min(root.width - width - root.edgeMargin,
                                              root.lensCenterX - width / 2))
        y: (root.lensBottom + 10 + height <= root.height - root.edgeMargin)
           ? root.lensBottom + 10
           : Math.max(root.edgeMargin, root.lensTop - 10 - height)

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 8

            Text {
                objectName: "loupeReadout"
                anchors.verticalCenter: parent.verticalCenter
                text: root.magnificationText(root.magnification)
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 12
                font.bold: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.pinned ? "Pinned" : "Following"
                color: root.pinned ? theme.gold : theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 11
            }
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18
                // A REAL glyph, never a text mark (the semantic-icon-audit law).
                ComicReaderIcon {
                    id: closeGlyph
                    anchors.fill: parent
                    kind: "close"
                    accessibleName: "Close the loupe"
                    ink: closeMa.containsMouse ? theme.ink : theme.inkDim
                }
                MouseArea {
                    id: closeMa
                    objectName: "loupeClose"
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    function tap() { root.dismiss() }
                    onClicked: tap()
                }
            }
        }
    }
}

// ComicReaderPagesOverlay — the temporary Pages filmstrip (Task 6, plan 2026-07-28).
//
// The permanent gold rail answers "where am I?". The Pages command answers "what is around me?" —
// and Hemanth picked its shape twice, in his own words:
//
//   "temporary overlay, obviously"   (a docked shelf and a navigator takeover were both rejected)
//   "a clean thumbnail film strip like YacR's appletunes looking strip would be great"
//
// So: an iTunes/Cover-Flow-shaped strip with a DOMINANT CENTRE, raised directly above the gold rail,
// drawn OVER the comic — which never shifts to make room for it. Selecting a thumbnail jumps and
// dismisses; Escape or a click on the comic dismisses without moving a single page.
//
// FOUR RULES, and each one is a real defect this component is built to avoid:
//
//   1. VIRTUALIZED. A 1,452-page volume must instantiate a handful of thumbnails, not 1,452 — and a
//      CLOSED filmstrip must instantiate none at all (the model is gated on `open`, so a shut
//      surface holds nothing). This reader exists because of a stutter; a filmstrip that hitches on
//      open would be a self-inflicted one.
//   2. THE THUMBNAIL TIER, ALWAYS. Every request goes through core.imageUrl(page, "thumbnail"),
//      which Task 2 caps at kThumbnailMaxWidth (240px). Asking for "hq" here would pull a
//      full-resolution scan per visible thumbnail and blow the scaled cache the reading surface
//      depends on. The gate asserts the tier both behaviourally and at the source level.
//   3. RTL MIRRORS THE VISUALS ONLY. The strip's sequence reverses for Manga order; the printed page
//      numbers never do. Page 16 is labelled 16 in both directions — that is locked design.
//   4. DISMISS NEVER MOVES. The canvas catcher and dismiss() emit dismissRequested() and nothing
//      else. There is exactly ONE door that can navigate (activateIndex), and it emits exactly one
//      jumpRequested and one dismissRequested per call, so the classic double-fire has nowhere to
//      come from.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome. This component owns no reading
// state: pageCount / currentPage / order / bookmarks are pushed in, and it raises jumpRequested /
// dismissRequested for the shell's ONE overlay coordinator to act on. It never writes currentPage
// and never calls a navigation function — which is what makes "dismiss without moving" structural
// rather than incidental.
//
// `open` is a RULE-level property, deliberately not `visible`: QQuickItem.visible is EFFECTIVE
// visibility, so a test asserting on a child's `visible` reads the parent's state too. `open` says
// what this surface believes, whatever its ancestors are doing.
//
// AUTO-HIDE: nothing here touches the 2500ms chrome/cursor timer, and nothing needs to. Task 5's
// HUD already holds the chrome while `activeOverlay` is non-empty (ComicReaderHud._holdChrome), the
// cursor follows chromeVisible, and the shell now folds this surface into `modalOpen`. An overlay
// that vanished from under the reader's hand would be a bug; the machinery that prevents it already
// existed, so adding a second timer here would be a second thing to keep in step.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    objectName: "readerPagesOverlay"

    // Fill the shell explicitly. anchors.fill resolves to 0x0 for this overlay mount (the same QML
    // quirk ComicReaderSettingsSheet documents); an explicit size binding tracks the parent
    // reliably and follows a resize / fullscreen flip.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= injected facts (never written back) =================
    property var    core: null                 // the ComicReaderCore seam — imageUrl() only
    property int    pageCount: 0
    property int    currentPage: 1
    property string order: "ltr"               // "ltr" (comic) | "rtl" (manga)
    property var    bookmarks: []              // 0-based page indices

    // ---- open state: the RULE, not the pixels (see the header note) ----
    property bool open: false
    visible: open

    // ================= intents =================
    signal jumpRequested(int page)             // 1-based, exactly once per activation
    signal dismissRequested()                  // close me; the reading position is untouched

    // ================= the chrome this surface has to live between =================
    // Mirrors ComicReaderHud's own numbers: the title strip (38) + the command bar (46) at the top,
    // the gold rail (54) at the bottom. They are properties rather than literals so a shell that
    // ever changes the chrome can say so in one place instead of this file drifting silently.
    property int chromeTopInset: 84
    property int railHeight: 54

    Theme { id: theme }

    readonly property color cBand:     Qt.rgba(6 / 255, 6 / 255, 8 / 255, 0.93)
    readonly property color cHairline: Qt.rgba(1, 1, 1, 0.07)
    readonly property color cSlot:     Qt.rgba(1, 1, 1, 0.05)

    // ================= pure geometry / vocabulary (harness-callable, like ComicReaderInput) =================

    // The centred slot. -1 when there is no book, so every consumer below has one thing to test.
    readonly property int centeredIndex:
        pageCount > 0 ? Math.max(0, Math.min(pageCount - 1, Math.round(currentPage) - 1)) : -1

    // The centre thumbnail's size, scaled off the reader's height so the strip is a filmstrip on a
    // laptop and on a 4K panel alike, and clamped so it can never eat the page it is describing.
    readonly property int thumbHeight: Math.max(96, Math.min(178, Math.round(root.height * 0.24)))
    readonly property int thumbWidth: Math.round(thumbHeight * 0.70)   // portrait comic page

    // The Cover-Flow ramp: full size at the centre, shrinking with distance, FLAT beyond `scaleReach`.
    // The flat far field is not cosmetic — it makes every thumbnail outside the bulge exactly the same
    // width, so the ListView's own position estimate for a 1,452-page model is right rather than
    // approximately right, which is what lets a jump to page 700 land centred on the first pass.
    // Tuned against the render (agents/comicreader-pages-task6-preview.png): a reach of 3 read as a
    // flat utility strip with one highlighted tile — the "grid of equal tiles" the design ledger
    // rules out. Five steps of 10% is the gentler Cover-Flow bulge Hemanth asked for by name, and it
    // still leaves the whole far field uniform.
    readonly property real minThumbScale: 0.52
    readonly property real scaleFalloff: 0.10
    readonly property int  scaleReach: 5
    function scaleForIndex(index0) {
        if (centeredIndex < 0) return minThumbScale
        var d = Math.abs(Math.round(index0) - centeredIndex)
        if (d >= scaleReach) return minThumbScale
        return Math.max(minThumbScale, 1.0 - d * scaleFalloff)
    }

    // Which PAGE NUMBER occupies visual slot `slot`, counting from the LEFT of the screen. This is
    // the mirroring, and it is the only thing that mirrors: LTR runs 1,2,3…; RTL runs N,N-1,N-2…
    // while every thumbnail keeps printing its own true page number.
    function visualPageAt(slot) {
        if (pageCount <= 0) return 0
        var s = Math.max(0, Math.min(pageCount - 1, Math.round(slot)))
        return ((order === "rtl") ? (pageCount - 1 - s) : s) + 1
    }

    function isBookmarked(index0) {
        return !!bookmarks && bookmarks.indexOf(Math.round(index0)) >= 0
    }

    // THE ONE DOOR THAT NAVIGATES. One emit each, inline, no timer and no second path — which is
    // why "exactly once" is a property of the code rather than a hope about it. An index outside the
    // book is inert: clamping it would jump the reader to a page nobody asked for.
    function activateIndex(index0) {
        var i = Math.round(index0)
        if (pageCount <= 0 || i < 0 || i >= pageCount) return
        jumpRequested(i + 1)
        dismissRequested()
    }
    // Close, and ONLY close. Nothing here can move the book.
    function dismiss() { dismissRequested() }

    // ---- observation surface: the harness reads the REAL laid-out view through these, so the pure
    //      helpers above are anchored to what the ListView actually did instead of restating
    //      themselves. NaN means "that delegate is not realized", which is a legitimate answer for a
    //      virtualized strip and must not be confused with 0. ----
    property int liveThumbs: 0                                  // delegates currently instantiated
    readonly property real viewportCenterX: flow.contentX + flow.width / 2
    // The view's OWN notion of "current". Read-only here (reading never breaks a binding; the write
    // that does is the one centreNow() owns) and asserted by the gate, because this is precisely the
    // value the ListView silently reset to 0 on a model change — see centreNow().
    readonly property int flowCurrentIndex: flow.currentIndex
    function itemXAt(index0)      { var it = flow.itemAtIndex(index0); return it ? it.x : NaN }
    function itemWidthAt(index0)  { var it = flow.itemAtIndex(index0); return it ? it.width : NaN }
    function itemCenterX(index0)  { var it = flow.itemAtIndex(index0); return it ? (it.x + it.width / 2) : NaN }
    function labelTextAt(index0)  { var it = flow.itemAtIndex(index0); return it ? it.labelText : "" }
    function markOpacityAt(index0){ var it = flow.itemAtIndex(index0); return it ? it.markOpacity : NaN }
    // Press a thumbnail the way a click does — through the delegate's own door, not the root's — so
    // the gate proves the wiring a mouse actually reaches. False when that delegate isn't realized.
    function pressThumb(index0) {
        var it = flow.itemAtIndex(index0)
        if (!it) return false
        it.activate()
        return true
    }

    // THE ONE DOOR that puts the strip somewhere. Two things in here are load-bearing and both were
    // measured, not assumed:
    //
    //  * `flow.currentIndex` IS ASSIGNED HERE, not bound. A declarative `currentIndex:
    //    root.centeredIndex` looks right and is a trap: when the model goes 0 -> N (which is exactly
    //    what opening this surface does, since the model is gated on `open`) the ListView sets
    //    currentIndex to 0 ITSELF, and an imperative write from C++ DESTROYS the QML binding
    //    permanently. Measured: the view then centred index 0 for the rest of the session and the
    //    current page sat 809px off screen-centre. The codebase already carries this scar twice —
    //    the HUD's side-scroller thumb `y:` binding, and the cursor-shape assignment the shell
    //    refuses to make. So centreNow() owns currentIndex, and every mutation routes through here.
    //
    //  * forceLayout BEFORE positioning, then again. positionViewAtIndex can only measure a
    //    delegate that is realized; for anything outside the realized window it falls back to an
    //    average-size ESTIMATE, which lands close but not exact on a far jump (opening the strip at
    //    page 700 of 1452). The first forceLayout realizes the neighbourhood, the second pass then
    //    positions against real geometry. Cheap — this runs on open and on a page change, never per
    //    frame.
    function centreNow() {
        var i = root.centeredIndex
        if (i < 0 || flow.count <= 0) return
        flow.currentIndex = i
        flow.forceLayout()
        flow.positionViewAtIndex(i, ListView.Center)
        flow.forceLayout()
        flow.positionViewAtIndex(i, ListView.Center)
    }

    onOpenChanged: if (open) Qt.callLater(root.centreNow)
    onCurrentPageChanged: if (open) Qt.callLater(root.centreNow)
    onPageCountChanged: if (open) Qt.callLater(root.centreNow)
    onOrderChanged: if (open) Qt.callLater(root.centreNow)

    // Decode-refresh dependency, the same trick the strip surface uses: imageUrl() folds a per-page
    // rev that bumps on pageReady, and that read happens in C++ where a QML binding cannot see it.
    // Bumping this counter re-drives every thumbnail's `source` so a page that decodes while the
    // filmstrip is open actually appears instead of staying an empty slot.
    property int readyRev: 0
    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageReady(page) { root.readyRev += 1 }
        // The Image panel adjusted the picture (Task 7). The thumbnails ride the same
        // delivery path as the page, so a rotated or brightened book must show rotated,
        // brightened thumbnails — and imageUrl()'s render revision is a C++-side read a QML
        // binding cannot see, so this bump is what makes the strip re-request.
        function onRenderProfileChanged() { root.readyRev += 1 }
    }

    // ================= the canvas catcher =================
    // "Escape or clicking the comic dismisses without moving." It covers the comic only: the chrome
    // bands keep working, so Back, the commands and the rail are all still reachable with the strip
    // up. It emits dismissRequested and nothing else — there is no navigation path through here.
    MouseArea {
        objectName: "pagesDismissCatcher"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.chromeTopInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.railHeight
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.ArrowCursor
        function tap() { root.dismiss() }
        onClicked: tap()
    }

    // ================= the filmstrip band, directly above the gold rail =================
    Rectangle {
        id: band
        objectName: "pagesBand"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.railHeight
        height: root.thumbHeight + 56
        color: root.cBand
        // Fades IN only. A Behavior that also gated `visible` would be a trap: an offscreen harness
        // never ticks animations, so the strip would be permanently invisible (and delegate-less)
        // under test. Root visibility stays a plain rule; only the paint eases.
        opacity: root.open ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

        // click-swallower (floating-panel house law): the band's empty ground must not fall through
        // to the catcher below and dismiss the surface you are reading. Declared FIRST so the
        // thumbnails sit above it and still take their own clicks.
        MouseArea {
            id: bandSwallow
            objectName: "pagesBandSwallow"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            function tap() { /* swallowed on purpose */ }
            onClicked: tap()
        }

        Rectangle {                                  // hairline lid, matching the HUD's chrome edges
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 1
            color: root.cHairline
        }

        // ---- the flow ----
        ListView {
            id: flow
            objectName: "pagesFilmstrip"
            anchors.fill: parent
            anchors.topMargin: 10
            anchors.bottomMargin: 8
            orientation: ListView.Horizontal
            // MODEL GATED ON `open`: a closed filmstrip holds zero delegates and zero decoded
            // thumbnails. A temporary surface should cost nothing while it is away.
            model: root.open ? root.pageCount : 0
            // NO `currentIndex:` binding here — see centreNow(), which owns it. The view rewrites
            // currentIndex itself on a model reset, and that write would kill the binding for good.
            layoutDirection: root.order === "rtl" ? Qt.RightToLeft : Qt.LeftToRight
            spacing: 12
            // Modest, like the strip surface: enough to keep the bulge's neighbours warm, nowhere
            // near enough to realize a book.
            cacheBuffer: Math.round(root.thumbWidth * 3)
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: false      // centreNow() owns the position; no second mover
            clip: true

            // A resize (window, fullscreen) rescales the slots, so re-centre. NOT onCurrentIndexChanged:
            // centreNow() assigns currentIndex itself, and reacting to that would be a loop.
            onWidthChanged: if (root.open) Qt.callLater(root.centreNow)

            delegate: Item {
                id: cell
                required property int index

                readonly property bool isCurrent: index === root.centeredIndex
                readonly property real thumbScale: root.scaleForIndex(index)
                // Exposed for the gate: the printed number and whether the bookmark mark is showing.
                readonly property string labelText: pageLabel.text
                readonly property real markOpacity: mark.opacity

                // The delegate's OWN door, so a click and the harness take the same path into the
                // one navigating function.
                function activate() { root.activateIndex(index) }

                width: Math.round(root.thumbWidth * thumbScale)
                height: flow.height

                // page thumbnail
                Rectangle {
                    id: slot
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Math.round((root.thumbHeight - height) / 2)
                    width: parent.width
                    height: Math.round(root.thumbHeight * cell.thumbScale)
                    color: root.cSlot
                    // GOLD IS SPARING and structural: only the page you are actually on wears it.
                    border.width: cell.isCurrent ? 2 : 1
                    border.color: cell.isCurrent ? theme.gold
                                                 : (cellMa.containsMouse ? theme.edge : root.cHairline)
                    opacity: cell.isCurrent ? 1.0 : (cellMa.containsMouse ? 0.95 : 0.72)

                    Image {
                        id: thumb
                        anchors.fill: parent
                        anchors.margins: cell.isCurrent ? 2 : 1
                        // THE THUMBNAIL TIER. readyRev folded in so a decode landing re-requests the
                        // fresh (?rev=N) url, exactly as ComicReaderStripSurface does.
                        source: (root.readyRev, (root.core && root.core.imageUrl)
                                 ? root.core.imageUrl(cell.index, "thumbnail") : "")
                        asynchronous: true
                        cache: true
                        retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                        // Screen-sized, never source-sized: ask for the pixels this slot shows. The
                        // provider clamps to 240px on top of this, so the request can only ever be
                        // smaller than the cap, never larger.
                        sourceSize.width: Math.max(1, root.thumbWidth)
                        mipmap: true
                    }

                    // bookmark mark — the filmstrip's half of "bookmarks mark both the thumbnails
                    // and the progress ticks" (the rail already draws the ticks off the same list).
                    ComicReaderIcon {
                        id: mark
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 3
                        width: 13; height: 13
                        kind: "bookmark"
                        accessibleName: "Bookmarked"
                        ink: theme.gold
                        opacity: root.isBookmarked(cell.index) ? 1 : 0
                    }
                }

                // The page NUMBER, always truthful. RTL reverses where this thumbnail is DRAWN, never
                // what it is called.
                Text {
                    id: pageLabel
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: slot.bottom
                    anchors.topMargin: 8
                    text: String(cell.index + 1)
                    color: cell.isCurrent ? theme.gold : theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: cell.isCurrent ? 14 : 12
                    font.bold: cell.isCurrent
                }

                MouseArea {
                    id: cellMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cell.activate()
                }

                // Realizing IS the decode-request trigger: imageUrl() below is pure string
                // construction, and the provider only ever SERVES an already-decoded page — it
                // never triggers one. Without this, a page the reader has never actually visited
                // via Single/Pair/Strip stayed permanently blank here, no matter how long the
                // strip sat open (the bug this delegate exists to fix). requestThumbnail() is
                // low-priority and non-pinning, so browsing the strip can never starve or evict
                // what the real reading surface needs; readyRev (below) already re-requests the
                // url once the decode lands, so no further wiring is needed here.
                Component.onCompleted: {
                    root.liveThumbs += 1
                    if (root.core && root.core.requestThumbnail) root.core.requestThumbnail(index)
                }
                Component.onDestruction: root.liveThumbs -= 1
            }
        }

        // ---- edge fades. Without these the strip HARD-CLIPS at both screen edges and reads as a
        // truncated list; the whole point of a flow is that it runs off into the distance either
        // side of the page you are on. Plain Rectangles, so they intercept no input — a thumbnail
        // half-under a fade is still clickable (the codebase's own click-swallowers are MouseAreas
        // precisely because a bare Rectangle cannot swallow anything).
        component EdgeFade: Rectangle {
            width: 110
            anchors.top: parent.top
            anchors.bottom: parent.bottom
        }
        EdgeFade {
            anchors.left: parent.left
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: root.cBand }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        EdgeFade {
            anchors.right: parent.right
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: root.cBand }
            }
        }
    }
}

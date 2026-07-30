// ComicReaderSingleSurface — the Single Page reading surface (Task 4, overhaul plan 2026-07-28).
//
// The third layout beside Long Strip and Pair, approved by Hemanth during design ("Yes, we can add a
// single page mode"). It is a LAYOUT, orthogonal to order: a manga read in Single Page is still
// right-to-left. Nothing in this file knows or cares about direction — which page comes next is the
// shell's question, and the shell already answers it from `order`.
//
// WHAT IT DRAWS: one page, alone, on the black stage.
//
//   * FIT IS CONTAIN, not fit-width. This is the one place Single deliberately parts company with
//     the Pair surface. A pair is two pages wide, so fitting it to the viewport WIDTH fills the
//     frame; doing the same to a single portrait page would draw it far taller than the window and
//     force you to pan down every page at 100% zoom, which is a strip pretending to be a page
//     reader. Contain (min of the width fit and the height fit) shows the WHOLE page, centred, and
//     leaves pan for when you have actually zoomed in.
//   * TWO TIERS, stacked (Task 2's preview/hq split, first consumer). `preview` is a FAST transform
//     at a smaller width and lands first; `hq` is the reader's real page and fades over it in 90ms
//     once it completes. The eye reads that as the page sharpening, not as two loads.
//   * ZOOM 100–260% PRESERVES THE CENTROID. Whatever sat under the middle of the window before the
//     zoom step is still there after it. The Pair surface only clamps its existing pan (already an
//     improvement on the reader it replaced, which zeroed it and teleported you to a corner); a
//     single page is small enough on screen that plain clamping still visibly slides the art out
//     from under you, so this one does the arithmetic properly.
//   * PAN CLAMPS to the zoomed page's own bounds, so you can never drag past the paper into black.
//   * WAITING is a restrained placeholder, FAILURE is the typed placard — the same two components
//     the Pair surface uses, so the two paged layouts behave like one reader.
//
// PRESENTED (the Task 4 seam, consumed by Task 11): `presented(anchorPage, withinPageFraction)`
// fires when this surface has actually put the page's pixels on screen — not when it was asked to.
// It is emitted ONCE per page (whichever tier lands first wins), so Task 11 can gate progress-saving
// on a page the reader genuinely saw. Nothing consumes it yet; that is Task 11's job, and the signal
// being unused-but-correct until then is expected.
//
// INJECTABLE + GUARDED, exactly like the other two surfaces: `core` is injected by the shell and
// every `core.` use is guarded, so a partial fake (the shell harness's Task-9 stub has no imageUrl)
// degrades to drawing nothing rather than erroring. All side effects (setVisible) are gated behind
// `active`, so an unmounted Single surface never touches the shared decode pool.

import QtQuick

Item {
    id: root

    // ---- injectable seam + inputs (bound by the shell) ----
    property var core: null
    property bool active: false
    property int currentPage: 1              // 1-based

    // ---- zoom/pan (surface-owned; pan resets on a page change, zoom persists across turns) ----
    property int zoomPercent: 100
    property real panX: 0
    property real panY: 0

    // ---- outputs ----
    // The page whose pixels are now on screen, and how far down it the viewport sits. The fraction is
    // always 0 here: a single page IS the viewport's unit of travel, so there is no "part way through
    // it" the way there is in Long Strip. It rides in the signature so all three surfaces speak one
    // shape and Task 11 has one handler, not three.
    signal presented(int anchorPage, real withinPageFraction)

    // Decode-refresh dependencies. Same reason as the other two surfaces: imageUrl()'s ?rev= bumps
    // C++-side on pageReady and pageInfo() is a plain call, so bindings that read either would never
    // re-run when the decode lands — the page would stay black until you navigated away and back.
    property int readyRev: 0
    property int failedRev: 0
    property int entryRev: 0
    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageReady(page)   { root.readyRev += 1 }
        function onPageFailed(page, code) { root.failedRev += 1 }
        function onEntryChanged()    { root.entryRev += 1; root._onPageShown() }
        function onPairingChanged()  { root.entryRev += 1 }
    }

    clip: true
    Rectangle { anchors.fill: parent; color: "#000000" }   // full-bleed black stage

    // ================= the page =================
    readonly property int pageIndex: Math.max(0, currentPage - 1)

    // Failure is the BACKEND's verdict for THIS page — not a pair's, and not a local cache of the
    // pageFailed signal. Single Page shows one page, so the unit-level presentationForPage() would be
    // the wrong question here: it would make page 5 wait on page 4 in a layout that never draws page
    // 4. pageInfo() is the per-page truth, and it also self-heals (a MissingFile page that comes back
    // reports "none" again), which a locally cached failure map would not.
    function _pageErrorCode(index) {
        if (index < 0 || !core || !core.pageInfo) return ""
        var info = core.pageInfo(index)
        if (!info || info.error === undefined) return ""
        var e = String(info.error)
        return (e === "" || e === "none") ? "" : e
    }
    readonly property string errorCode: (root.readyRev, root.failedRev, root.entryRev,
                                         _pageErrorCode(root.pageIndex))
    readonly property bool hasError: errorCode.length > 0

    // ================= zoom / pan geometry =================
    readonly property int clampedZoom: Math.max(100, Math.min(260, zoomPercent))
    readonly property real zoomFactor: clampedZoom / 100.0

    // Decode cap, carried over from the Pair surface: cap the decoded width so a page costs what it
    // is DISPLAYED at, but raise the cap with zoom or magnification would just show a bigger blur.
    // It is derived from the ZOOM, never from the viewport — sourceSize is part of an Image's cache
    // key, so a cap that tracked the window would re-decode every page on entering fullscreen (the
    // exact bug the strip's Screen-derived cap fixed).
    readonly property int srcCapW: clampedZoom >= 180 ? 2800 : (clampedZoom > 100 ? 2048 : 1400)
    // The preview tier is a stand-in for about one frame, so it asks for HALF the width AND takes the
    // fast transform: both halves of "first pixels, soonest".
    readonly property int previewCapW: Math.max(320, Math.round(srcCapW / 2))

    // NATURAL SIZE from the BACKEND's header geometry, with the Image's own decoded size as the
    // fallback — the same order and the same reason as the Pair surface: `sourceSize.width` makes the
    // provider hand back every page normalised to the cap, so an Image's implicit size cannot be
    // trusted to describe the paper. (Here it matters less than it does for a pair — there is no
    // second page to be mis-scaled against — but a page whose true aspect is unknown would be fitted
    // to the wrong box, which is a visible letterbox error.)
    function _naturalSize() {
        var w = 0, h = 0
        if (core && core.pageInfo) {
            var info = core.pageInfo(root.pageIndex)
            if (info) {
                w = info.sourceWidth  > 0 ? info.sourceWidth  : 0
                h = info.sourceHeight > 0 ? info.sourceHeight : 0
            }
        }
        if (!(w > 0 && h > 0)) { w = hqImage.implicitWidth; h = hqImage.implicitHeight }
        if (!(w > 0 && h > 0)) { w = previewImage.implicitWidth; h = previewImage.implicitHeight }
        return { w: (w > 0 ? w : 0), h: (h > 0 ? h : 0) }
    }
    readonly property var _nat: (root.readyRev, root.entryRev, _naturalSize())
    readonly property real naturalWidth: _nat.w
    readonly property real naturalHeight: _nat.h

    // CONTAIN, then zoom. With nothing known yet the page falls back to the full frame, which is the
    // same box the placeholder wants — so the unit does not jump when the real geometry lands.
    readonly property real fitScale: (_nat.w > 0 && _nat.h > 0)
        ? Math.min(width / _nat.w, height / _nat.h) : 0
    readonly property real pageScale: fitScale * zoomFactor
    readonly property real pageWidth:  _nat.w > 0 ? _nat.w * pageScale : width
    readonly property real pageHeight: _nat.h > 0 ? _nat.h * pageScale : height

    readonly property real panXMax: Math.max(0, pageWidth - width)
    readonly property real panYMax: Math.max(0, pageHeight - height)
    // Centred while it fits; pan-driven once it is bigger than the frame (where the centring term is
    // 0 anyway, so this is one expression, not two cases).
    readonly property real pageX: pageWidth  <= width  ? (width - pageWidth) / 2   : -panX
    readonly property real pageY: pageHeight <= height ? (height - pageHeight) / 2 : -panY

    function setZoom(pct) {
        var next = Math.max(100, Math.min(260, Math.round(pct)))
        if (next === clampedZoom) { zoomPercent = next; return }
        // KEEP WHAT YOU WERE LOOKING AT. Read the page point under the middle of the window BEFORE
        // the zoom (as a 0..1 fraction of the page), then put that same point back under the middle
        // afterwards. Falls back to the page centre when there is no geometry yet.
        var fx = pageWidth  > 0 ? (width  / 2 - pageX) / pageWidth  : 0.5
        var fy = pageHeight > 0 ? (height / 2 - pageY) / pageHeight : 0.5
        zoomPercent = next
        panX = Math.max(0, Math.min(panXMax, fx * pageWidth  - width  / 2))
        panY = Math.max(0, Math.min(panYMax, fy * pageHeight - height / 2))
    }
    function zoomIn()  { setZoom(clampedZoom + 20) }
    function zoomOut() { setZoom(clampedZoom - 20) }
    function panBy(dx, dy) {
        panX = Math.max(0, Math.min(panXMax, panX + dx))
        panY = Math.max(0, Math.min(panYMax, panY + dy))
    }

    // ================= page lifecycle: reset PAN (zoom persists), pin, drive the decode ==========
    function _onPageShown() {
        // A new page resets the PAN to origin — never the zoom. Same law as the Pair surface: a
        // magnified volume that snapped back to 100% on every turn would be unreadable.
        panX = 0; panY = 0
        if (!active) return
        // The index is computed FRESH here, not read off the `pageIndex` binding. A binding's
        // re-evaluation order versus the change handler that triggered it is NOT guaranteed, so
        // reading it here pinned the page you just LEFT — caught by the surfaces gate, and the same
        // trap the Pair surface documents on _currentUnit().
        var idx = Math.max(0, currentPage - 1)
        if (core && core.setVisible) core.setVisible([idx])
    }
    onCurrentPageChanged: _onPageShown()
    // Becoming the mounted surface with pixels already up IS a presentation — the layout switched and
    // the reader is now looking at that page — so the notice is re-checked here, not only on a decode.
    onActiveChanged: { _onPageShown(); _notePresented() }

    // ---- presented(): fired once per page, the moment either tier has pixels up ----
    // Derived from the two Images rather than from a status handler on each, so preview-then-hq is one
    // state change and therefore ONE presentation; and so an already-cached page counts the instant
    // this surface becomes the mounted one.
    readonly property bool pixelsOnScreen: previewImage.status === Image.Ready
                                           || hqImage.status === Image.Ready
    property int _presentedPage: -1
    function _notePresented() {
        if (!active || !pixelsOnScreen) return
        if (_presentedPage === root.currentPage) return
        _presentedPage = root.currentPage
        root.presented(root.currentPage, 0)
    }
    onPixelsOnScreenChanged: _notePresented()

    Item {
        id: pageBox
        x: root.pageX
        y: root.pageY
        width: root.pageWidth
        height: root.pageHeight

        // the unit is still coming — one quiet panel, exactly where the page will land
        ComicReaderUnitPlaceholder {
            anchors.fill: parent
            shown: !root.hasError && !root.pixelsOnScreen
        }

        // PREVIEW tier: fast transform, half width, first pixels on screen.
        Image {
            id: previewImage
            anchors.fill: parent
            visible: !root.hasError
            source: (root.readyRev, (root.core && root.core.imageUrl && root.pageIndex >= 0)
                    ? root.core.imageUrl(root.pageIndex, "preview") : "")
            asynchronous: true
            cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode,
                           // and without the pixmap cache every rebuild re-pays the provider's scale.
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: root.previewCapW
            mipmap: true                     // it is a downscale; without this it shimmers while scaling
        }

        // HQ tier: the reader's real page, faded over the preview as it completes.
        Image {
            id: hqImage
            anchors.fill: parent
            visible: !root.hasError
            source: (root.readyRev, (root.core && root.core.imageUrl && root.pageIndex >= 0)
                    ? root.core.imageUrl(root.pageIndex, "hq") : "")
            asynchronous: true
            cache: true
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: root.srcCapW
            mipmap: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
    }

    // typed error placard — over the whole frame, because in Single Page this page IS the whole frame
    ComicReaderUnitError {
        anchors.fill: parent
        visible: root.hasError
        code: root.errorCode
        pageIndex: root.pageIndex
    }

    // ---- test/HUD readbacks: these READ THE ITEMS rather than re-deriving the math, so they cannot
    // drift from what is actually drawn. ----
    readonly property real drawnX: pageBox.x
    readonly property real drawnY: pageBox.y
    readonly property real drawnWidth: pageBox.width
    readonly property real drawnHeight: pageBox.height
    readonly property alias previewSource: previewImage.source
    readonly property alias hqSource: hqImage.source
    readonly property bool errorVisible: root.hasError
}

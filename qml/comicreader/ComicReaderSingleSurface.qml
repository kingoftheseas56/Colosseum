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
//   * FAILURE is the typed placard, and WAITING is the restrained placeholder — the same two
//     components the Pair surface uses.
//
//     They do NOT behave identically on a page turn, and the difference is worth knowing: this
//     surface keeps the OUTGOING page on screen while the next one decodes (retainWhileLoading holds
//     the old pixmaps, and the placeholder is declared first so it sits behind them), whereas the
//     Pair surface shows the placeholder, because its gate hides the images outright and there is
//     nothing left to retain. So Single's placeholder is in practice only reachable on a FIRST open,
//     or on a page whose geometry is known before any pixels exist. Which of the two feels better on
//     a turn is Hemanth's call, not something this file should quietly decide.
//
// PRESENTED (the Task 4 seam, consumed by Task 11): `presented(anchorPage, withinPageFraction)`
// fires when this surface has actually put the page's pixels on screen — not when it was asked to.
// It is emitted ONCE per page (whichever tier lands first wins), so Task 11 can gate progress-saving
// on a page the reader genuinely saw. Nothing consumes it yet; that is Task 11's job, and the signal
// being unused-but-correct until then is expected.
//
// INJECTABLE + GUARDED, exactly like the other two surfaces: `core` is injected by the shell and
// every `core.` use is guarded, so a partial fake (the shell harness's Task-9 stub has no imageUrl)
// degrades to drawing nothing rather than erroring.
//
// EVERY reach for the backend is gated on `active` — the setVisible pin AND both image `source`
// bindings. The sources matter as much as the pin: `currentPage` is bound to the shell's page
// unconditionally, so while Long Strip is the mounted layout this surface's page number follows the
// column as it scrolls. Ungated, that issued two provider requests per page scrolled past, at request
// sizes the strip does not share and therefore as separate pixmap-cache entries competing with the
// strip's own. (Measured: with active:false the urls were live and tracked currentPage. The Pair
// surface never had this, because its sources flow through `unit`, which is already active-gated.)

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
        // The Image panel adjusted the picture (Task 7) — the same refresh dependency as a
        // decode landing: imageUrl() folds the render revision C++-side, invisibly to QML, so
        // without this bump the `source` binding never re-evaluates and QML's own pixmap
        // cache keeps serving the pre-adjustment page.
        function onRenderProfileChanged() { root.readyRev += 1 }
        function onPageFailed(page, code) { root.failedRev += 1 }
        function onEntryChanged()    {
            root.entryRev += 1
            // A NEW BOOK has to be able to present the same page number as the last one. Without this
            // reset, opening book B on page 1 straight after reading book A page 1 emitted nothing at
            // all, because the marker still said "page 1 is presented". (Measured: 0 emissions.)
            root._presentedPage = -1
            root._onPageShown()
            root._checkPresented()
        }
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
    // is DISPLAYED at, not its full scan resolution — but raise the cap as you zoom in, or
    // magnification would just be showing you a bigger blur.
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
        // ...and a NEW page's hq layer genuinely has not arrived, so the M7 latch below re-arms.
        // Deferred re-check as well as the reset, because onStatusChanged CANNOT arm it on a page
        // whose pixmap is already cached: the status goes Ready -> Ready and never changes, so the
        // handler never runs and the latch would stay false on exactly the pages most likely to be
        // zoomed. (Same shape, and the same reason, as _checkPresented below.)
        _hqEverReady = false
        Qt.callLater(root._armHqLatch)
        if (!active) return
        // The index is computed FRESH here, not read off the `pageIndex` binding. A binding's
        // re-evaluation order versus the change handler that triggered it is NOT guaranteed, so
        // reading it here pinned the page you just LEFT — caught by the surfaces gate, and the same
        // trap the Pair surface documents on _currentUnit().
        var idx = Math.max(0, currentPage - 1)
        if (core && core.setVisible) core.setVisible([idx])
    }
    onCurrentPageChanged: { _onPageShown(); _checkPresented() }
    // Becoming the mounted surface with pixels already up IS a presentation — the layout switched and
    // the reader is now looking at that page — so the notice is re-checked here, not only on a decode.
    onActiveChanged: { _onPageShown(); _checkPresented() }

    // ---- presented(): fired once per page, the moment either tier has pixels up ----
    // Derived from the two Images rather than from a status handler on each, so preview-then-hq is one
    // state change and therefore ONE presentation.
    // ONE RULE, shared with the other two surfaces: there is something on screen for this page when
    // its pixels have arrived OR it failed terminally and its typed placard is the honest answer. Not
    // named "pixelsOnScreen", because a placard is not pixels of the page and the name would be a lie
    // — the strip and the Pair surface follow the same rule under the same reasoning: presented()
    // means "the reader can see this position's content, or an explicit account of why not", and Task 11
    // must not sit waiting to bank a position that can never render.
    readonly property bool contentOnScreen: previewImage.status === Image.Ready
                                            || hqImage.status === Image.Ready
                                            || root.hasError
    // M7: "the hq layer has real pixels for THIS page", which is what lets a zoom step reload it
    // without dimming what is already on screen. Written from two places — hqImage's onStatusChanged
    // (a cold page arriving) and this deferred arm (a warm page, whose status never changes) — and
    // re-armed per page in _onPageShown, because a NEW page's hq genuinely has not arrived.
    property bool _hqEverReady: false
    function _armHqLatch() { if (hqImage.status === Image.Ready) _hqEverReady = true }
    property int _presentedPage: -1
    function _notePresented() {
        if (!active || !contentOnScreen) return
        if (_presentedPage === root.currentPage) return
        _presentedPage = root.currentPage
        root.presented(root.currentPage, 0)
    }
    // contentOnScreen alone is NOT enough to NOTICE a presentation, and there are two holes it leaves:
    //
    //   * A NEW BOOK opening on the same page number the last one was left on. The marker still says
    //     that page is presented, so nothing fires. Unconditional — no cache behaviour involved.
    //     Handled by resetting the marker on entryChanged, above.
    //   * A turn onto a page whose pixmaps are still in QQuickPixmapCache: Image.status goes straight
    //     to Ready with no dip, so this property never CHANGES and nothing is emitted, while the
    //     marker still points at the page the reader left. MEASURED 2026-07-30 at both regimes,
    //     because two reviewers got opposite answers: at 140/280px request sizes the turn back onto a
    //     just-left page keeps BOTH tiers Ready and fires zero times; at this surface's real caps
    //     (700 preview + 1400 hq -> ~3 MB + ~11 MB, against QQuickPixmapStore's 10 MB desktop limit
    //     for UNREFERENCED pixmaps) releasing the page evicts both, so the turn back genuinely
    //     re-loads and the status does dip. In other words today's app is saved by a decoded page
    //     being too big to cache — an undocumented limit that moves with the cap, the screen and the
    //     Qt version, and one that a bigger cache or a smaller cap silently removes.
    //
    // So the page-change path re-checks too, deferred through Qt.callLater so the source/status
    // bindings have settled by the time it reads them. Firing exactly once is the MARKER's job, not
    // callLater's — but callLater does collapse repeats within one pass (measured: three schedules of
    // the same method, one invocation), so a turn that also dips the status queues no extra work.
    function _checkPresented() { Qt.callLater(root._notePresented) }
    onContentOnScreenChanged: _notePresented()

    Item {
        id: pageBox
        x: root.pageX
        y: root.pageY
        width: root.pageWidth
        height: root.pageHeight

        // the unit is still coming — one quiet panel, exactly where the page will land
        ComicReaderUnitPlaceholder {
            anchors.fill: parent
            // No `!hasError` term: a failed page is already "content on screen" (its placard), so the
            // one condition covers both.
            shown: !root.contentOnScreen
        }

        // PREVIEW tier: fast transform, half width, first pixels on screen.
        Image {
            id: previewImage
            anchors.fill: parent
            visible: !root.hasError
            source: (root.readyRev, (root.active && root.core && root.core.imageUrl
                                     && root.pageIndex >= 0)
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
            source: (root.readyRev, (root.active && root.core && root.core.imageUrl
                                     && root.pageIndex >= 0)
                    ? root.core.imageUrl(root.pageIndex, "hq") : "")
            asynchronous: true
            cache: true
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: root.srcCapW
            mipmap: true
            // LATCHED, not bound to the live status. srcCapW is a step function of the zoom, so
            // crossing 100 or 180 changes sourceSize — part of an Image's cache key — and
            // re-requests this layer. Bound to the live status, hq then faded OUT to the retained
            // lower-resolution pixels and back IN once the new scale landed: two soft flashes on the
            // way from 100% to 260%. (Measured: 60ms after setZoom(200), status was Loading and
            // opacity had fallen to 0.26.) retainWhileLoading keeps the previous pixels on screen
            // through that reload, so holding opacity at 1 shows the page slightly soft for a moment
            // and then sharpens — the intended feel — instead of dimming it. The latch re-arms per
            // page, in _onPageShown, because a new page's hq really has not arrived.
            //
            // The rule is a NAMED property that `opacity` is bound to, because the Behavior below
            // makes the animated value untestable: a synchronous read straight after a target flip
            // still returns the OLD value (measured — target 1 -> 0 reads 1.000 in the same beat), so
            // a harness asserting on `opacity` would pass against the flashing version too. The
            // readback at the bottom exposes THIS property, which is the rule itself.
            property real targetOpacity: (status === Image.Ready || root._hqEverReady) ? 1 : 0
            onStatusChanged: if (status === Image.Ready) root._hqEverReady = true
            opacity: targetOpacity
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

    // ---- WHAT THIS SURFACE IS DRAWING, and where (Task 9, the Loupe's one question) ----
    // [{ page (0-based), url, x, y, width, height }] in THIS surface's coordinates — which are the
    // shell's, since all three surfaces fill it. The three surfaces answer in ONE shape so the lens
    // has one code path for Single, Pair and Long Strip; the boxes are read off the DRAWN item
    // rather than re-derived, so they cannot drift from what is on screen.
    //
    // The url is the hq tier WITHOUT this surface's decode cap: the caller states its own request
    // size, and the Loupe deliberately asks for far more than the screen shows. An inactive surface
    // is drawing nothing and says so.
    function visiblePageRects() {
        if (!active || pageIndex < 0) return []
        if (!(pageBox.width > 0) || !(pageBox.height > 0)) return []
        return [{ page: pageIndex,
                  url: (core && core.imageUrl) ? core.imageUrl(pageIndex, "hq") : "",
                  x: pageBox.x, y: pageBox.y, width: pageBox.width, height: pageBox.height }]
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
    // The hq layer's opacity RULE (the value the Behavior is animating toward), so the surfaces gate
    // can prove the latch holds it at 1 through a zoom step instead of trusting the comment. The
    // live `opacity` is deliberately not what is exposed: an in-flight Behavior means a synchronous
    // read cannot see a flash at all. Its `status` rides along so the fixture can prove the zoom step
    // genuinely re-requested the layer rather than asserting against a no-op.
    readonly property alias hqTargetOpacity: hqImage.targetOpacity
    readonly property alias hqStatus: hqImage.status
}

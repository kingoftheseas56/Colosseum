// ComicReaderStripSurface — the Long Strip reading surface (Task 10).
//
// The manga-default vertical reader: one continuously-scrolling column of full-bleed pages on
// black. It is a THIN painter over the Task-7 backend — "QML paints, C++ decides":
//
//   * GEOMETRY IS AUTHORITATIVE. A ListView is bound to `core.stripModel` (the Task-6
//     ComicReaderStripModel, roles pageIndex/top/displayWidth/displayHeight/ready/errorCode). Each
//     delegate takes its height/width from the MODEL roles, never from the loaded Image's implicit
//     size — so a page decoding to a taller-than-estimated size never reflows/jerks the column.
//   * VIRTUALIZED. ListView keeps only near-viewport delegates (modest cacheBuffer); on scroll the
//     surface reports the viewport to core.setStripViewport(top,height) + setStripViewportWidth(w)
//     THROTTLED to at most once per frame (a 16ms coalescing timer, not one call per contentY tick).
//   * ANTI-JUMP. core.stripCompensation(delta) — emitted when a page above the fold decodes to a new
//     height — is added straight to contentY so the read position doesn't shift under the reader.
//   * SMOOTH WHEEL. The family's float accumulator — ported from the reader this one replaced
//     (MangaReader.qml's FrameAnimation drain, itself TB2's), NOT re-derived. ~168px/notch intake
//     into a bounded backlog, 38% of that backlog drained per frame, sub-pixel float contentY.
//     THIS is the reading feel, and every clause of it is load-bearing — see the drain below.
//   * PER-PAGE FAILURE. core.pageFailed(page,code) shows a typed placard on THAT page's delegate
//     only (missing / decode / unsupported); the rest of the column keeps reading.
//
// INJECTABLE + GUARDED. `core` is injected by the shell (its ComicReaderCore seam). Every `core.`
// use is guarded, so the surface also survives the shell's Task-9 fake (which has no imageUrl /
// stripModel / setStripViewport) — it simply renders nothing until a real core is present.
//
// STATE-MUTATING SIGNALS ARE PROVENANCE-BLIND, GATED ONLY ON _programmatic. pageInView/scrolled fire
// on ANY real move of the column — wheel, keyboard (Space/PageUp/PageDown), a scrub-bar drag, Home/
// End — throttled to at most once per ~80ms window (Reader 1's pageTrack), never once per contentY
// tick. They fire NEVER on construction, resume, or compensation (those write contentY with
// _programmatic held true) — so mounting this surface never clobbers the shell's resumed page/
// fraction. The shell consumes pageInView/scrolled out; it puts the column somewhere by CALLING
// seekToPage()/haltScrollAt(), never by binding a fraction in (that would be a scroll -> fraction ->
// apply -> scroll loop).

import QtQuick
import QtQuick.Window   // Screen — the decode cap must not move when the WINDOW does

Item {
    id: root

    // ---- injectable seam + inputs (bound by the shell) ----
    property var core: null
    property bool active: true
    property bool rtl: false                 // strip is vertical; kept for parity / future affordances
    property int cacheScreens: 2             // cacheBuffer in viewport-heights (modest -> virtualized)

    // ---- AUTO-SCROLL (Task 8). MOTION ONLY. ----
    // The approved rule, verbatim: "Long Strip creates the vertical page flow; Auto-scroll only
    // supplies motion at the already chosen width. Starting or resuming Auto-scroll must never
    // resize the page." Structurally, not by promise: the drive below writes contentY and NOTHING
    // ELSE. There is no call to applyLayout / core.setStripLayout anywhere on this path, and the
    // width is not an input to any of it.
    //
    // The SHELL owns whether it is running (it is session-only state and every pause source lives
    // up there); this surface owns the pixels-per-frame. Both flow one way: in.
    property bool autoScrollRunning: false
    property real autoScrollSpeed: 1.0       // 0.25..3.0, clamped where it is used
    // 120px/s at 1x — a comfortable webtoon pace, and the number the plan fixes. At the default 78%
    // portrait width a page is roughly two viewport heights, so 1x reads a page in about 15s.
    readonly property real autoScrollBasePps: 120
    readonly property real autoScrollPixelsPerSecond:
        autoScrollBasePps * Math.max(0.25, Math.min(3.0, autoScrollSpeed))

    // ---- DECODE CAP (the lineage's sourceSize.width; the strip had none) ----
    // Without a cap, every page decodes and uploads its FULL source resolution — a scanned tankobon
    // page is routinely 2000-3000px wide — while it is displayed at a fraction of that. The cost is
    // paid on the render thread, once per page arriving on screen, which is why an uncapped strip
    // reads as smooth-but-intermittently-laggy rather than uniformly harsh: you only feel it when a
    // fresh page comes into view. The reader this one replaced capped its strip at 1100px.
    //
    // DELIBERATE DEVIATION from that flat 1100: it would be visibly soft on a large display, where
    // a page is shown wider than that. This caps to the SCREEN width instead — never smaller than
    // the lineage's 1100, never more than 2048 — so it can't be soft and can't be wasteful.
    //
    // The SCREEN, emphatically not the viewport. sourceSize is part of an Image's cache key, so
    // every change to this number re-decodes every visible page — each one paying a full-resolution
    // downscale in the provider. Tying it to the window meant entering fullscreen re-decoded the
    // whole visible column, and leaving it did so again: Hemanth's "going in and out of fullscreen
    // looks incredibly rough" (2026-07-26). The screen is the widest a page can ever be displayed,
    // so it is the correct upper bound AND it holds still while the window moves.
    readonly property int srcCapW:
        Math.max(1100, Math.min(2048, Math.ceil(Math.max(320, Screen.width) / 256) * 256))
    // ...and the PREVIEW tier's cap. Half the width AND the fast transform (Task 2's `preview` tier
    // picks both), which is the whole of "first pixels, soonest". Same rule and the same arithmetic
    // as the Single surface, so the three surfaces ask the delivery path for the same shape of work.
    //
    // A quarter of the pixels for the first paint matters MORE here than in the paged layouts: this
    // is the surface that meets new pages continuously, so the wait for hq is not once per page turn
    // but once per page scrolled past. The scaled tier was sized for this pair from the start —
    // ComicReaderCore's kScaledEntriesPerPage is 2 and says so out loud.
    readonly property int previewCapW: Math.max(320, Math.round(srcCapW / 2))

    // ---- outputs consumed by the shell / HUD (Task 11) ----
    // These three are PROVENANCE-BLIND: they fire for any non-programmatic move — wheel, keyboard,
    // scrub drag, Home/End. Only _programmatic writes (resume, compensation, layout anchoring) stay
    // silent. Gating them on "was it a wheel gesture" is what made reading with the spacebar file a
    // Continue record of page 1.
    signal pageInView(int page)              // 1-based page at the vertical center
    signal visiblePages(var indices)         // 0-based indices currently on screen
    signal scrolled(real fraction)           // 0..1 scroll fraction
    // The page whose pixels are ACTUALLY on screen, and how far down that page the viewport centre
    // sits (0 at its top edge, 1 at its bottom). Task 4's cross-surface seam: all three reading
    // surfaces speak this one shape so Task 11 can gate progress-saving on real presentation with a
    // single handler. In Long Strip the fraction is the only one of the three that is ever non-zero —
    // you genuinely stop part way down a page here — which is why it is in the signature at all.
    // Nothing consumes it yet; that is Task 11.
    signal presented(int anchorPage, real withinPageFraction)
    // Unlike the three above this one IS wheel-only — it means "a real gesture happened", not
    // "the position changed". Task 8 gave it its consumer at last: the shell pauses Auto-scroll on
    // it, which is exactly the question it was always answering ("was this a hand, or the machine")
    // and the reason it survived unconsumed for three tasks.
    signal manualNavigation()
    // The column reached the bottom while Auto-scroll was driving it. The shell clears its running
    // flag on this — the surface never writes that flag, so there is one owner and no way for the
    // two to disagree about whether the motion is live.
    signal autoScrollEnded()

    clip: true

    // full-bleed black stage — the page owns the screen
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- test/observation surface (harness reads these; also handy for the HUD later) ----
    property alias contentY: list.contentY
    readonly property int rowCount: list.count
    readonly property real contentHeight: list.contentHeight
    // Is the column parked at the bottom — or already gliding into it? The shell asks this to decide
    // whether a page-down should scroll or announce the end of the volume. It counts the IN-FLIGHT
    // backlog deliberately: without that, a second Space pressed while the first is still gliding
    // would read the not-yet-arrived position and announce the end while the page is still moving.
    //
    // AND IT REFUSES TO ANSWER ON ESTIMATED GEOMETRY (hardened after Codex's cross-review, which
    // caught this as a real defect). Until a page is decoded the column uses an ESTIMATED height for
    // it, so contentHeight is an underestimate and the scrollable span is short. Scroll down faster
    // than the decodes land and you reach "the end" while there is still book left — and the reader
    // announces the end of a volume you are in the middle of. Lying is worse than saying nothing, so
    // an unmeasured tail reports false and the shell simply scrolls instead.
    //
    // `readyRev` is read purely as a reactive dependency: it bumps on every pageReady, which is the
    // only signal QML gets that the backend learned a real page size (the per-page rev it folds into
    // imageUrl is a C++-side read QML cannot see). Same trick the `source` bindings below use.
    readonly property bool atEnd: {
        var _dep = readyRev                       // re-evaluate as decodes land
        var n = list.count
        if (n <= 0) return false                  // nothing loaded yet is NOT the end of anything
        if (!_lastRowMeasured()) return false     // geometry still an estimate — never announce
        var span = Math.max(0, list.contentHeight - list.height)
        if (span <= 0) return true                // the whole (measured) book fits the viewport
        return (list.contentY + _pendingWheelPx) >= span - 1
    }
    // Has the backend learned the LAST page's true size? pageInfo() omits geometry it has not
    // learned yet (ComicReaderCore::onMetaReady fills it from the file header ahead of the decode),
    // so a present, positive sourceHeight is the honest "this row's height is real, not estimated"
    // signal. A partial seam or a fake without sizes answers "not measured", which fails CLOSED.
    function _lastRowMeasured() {
        var n = list.count
        if (n <= 0 || !core || !core.pageInfo) return false
        var pi = core.pageInfo(n - 1)
        return !!pi && pi.sourceHeight !== undefined && Number(pi.sourceHeight) > 0
    }
    function itemAt(i) { return list.itemAtIndex(i) }
    function forceRelayout() { list.forceLayout() }

    // ---- internal flags ----
    property bool _programmatic: false       // suppress user-signal emission for resume/compensation
    property bool _draining: false           // the wheel drain is authoring contentY (don't resync smoothY)
    // a wheel gesture has occurred this session. NARROWED (B3): this used to ALSO gate
    // pageInView/scrolled emission in onContentYChanged — that was bug 1 (keyboard/scrub reading
    // never reported until one incidental wheel notch anywhere in the session silently "fixed" it).
    // Tracking is now provenance-blind (gated on _programmatic only, see onContentYChanged below).
    // Kept as wheel-specific session provenance for manualNavigation()'s potential future consumers
    // (e.g. HUD auto-hide reacting only to a real gesture, not every strip move) — currently
    // unconsumed by the shell (grep shows no onManualNavigation handler), so this flag has no live
    // reader today, but nothing else duplicates "did a wheel gesture happen" and removing it would
    // be an unrelated cleanup outside this bug fix's scope.
    property bool _userInteracted: false

    // Decode-refresh dependency. The C++ provider returns a NULL image for a not-yet-decoded page
    // and imageUrl() embeds a per-page rev that bumps on pageReady — but imageUrl() reading that rev
    // is a C++-side read QML CANNOT see, so an Image `source` bound only to {core,page} would never
    // re-request after the decode lands (page stays blank until the delegate is recreated). This
    // counter, bumped on every pageReady and folded into each `source` via the comma-sequence trick,
    // is the reactive dependency that re-drives the binding so the fresh (?rev=N) url is re-fetched.
    property int readyRev: 0

    // ================= per-page failure tracking =================
    property var failedPages: ({})           // 0-based page -> code string
    property int failedRev: 0                 // bumped so delegates re-evaluate _failText()
    function _markFailed(page, code) {
        failedPages[page] = String(code)
        failedRev += 1
    }
    function _clearFailed(page) {
        if (failedPages[page] !== undefined) {
            delete failedPages[page]
            failedRev += 1
        }
    }
    function _failText(page, errorCode) {
        var code = failedPages[page]
        if (code === undefined || code === "") {
            var e = Number(errorCode) || 0     // model ErrorCodeRole int: 1 missing / 2 decode / 3 unsupported
            code = e === 1 ? "missing_file" : e === 2 ? "decode_failed" : e === 3 ? "unsupported_image" : ""
        }
        switch (code) {
        case "missing_file":      return "Page missing"
        case "decode_failed":     return "Couldn't decode"
        case "unsupported_image": return "Unsupported format"
        default:                  return ""
        }
    }

    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageFailed(page, code) { root._markFailed(page, code) }
        // decode landed: clear any failure AND bump readyRev so the page's `source` re-requests the
        // freshly-decoded pixels (the ?rev= url changed C++-side, invisibly to QML).
        function onPageReady(page)        { root._clearFailed(page); root.readyRev += 1 }
        // The Image panel adjusted the picture (Task 7). Same refresh dependency as a
        // decode landing, and for the same reason: imageUrl() now folds the render
        // revision, and that read happens in C++ where a QML binding cannot see it. Without
        // this bump the `source` binding never re-evaluates, the url never changes, and QML's
        // own pixmap cache serves the pre-adjustment page for the rest of the session.
        function onRenderProfileChanged() { root.readyRev += 1 }
        function onStripCompensation(delta) { root._applyCompensation(delta) }
        // A fresh book (or a crossing) invalidates the retention memo. ComicReaderCore resets its
        // own last-swept range on every openEntry for exactly this reason — "a new book always
        // sweeps even if it opens on the page numbers the last one closed at" — and a QML memo that
        // did not reset alongside it would swallow the new entry's FIRST call and leave the window
        // wherever the previous volume left it.
        function onEntryChanged() { root._rangeFirst = -1; root._rangeLast = -1 }
    }

    function _applyCompensation(delta) {
        // a page above the fold changed height — shift our scroll position by the same delta so the
        // read position holds. Programmatic (never a user gesture).
        //
        // CLAMPED (E6): this was the last unclamped contentY write in the surface. Near the top of a
        // book a page above the fold can shrink, producing a negative delta larger than the scroll
        // position — contentY goes negative and the reader shows a black band above page 1 until
        // something else moves the view. Every other write already clamps; this one was simply
        // missed. The clamp is inert wherever there is room, so ordinary mid-book compensation is
        // unchanged.
        var maxY = Math.max(0, list.contentHeight - list.height)
        _programmatic = true
        list.contentY = Math.max(0, Math.min(maxY, list.contentY + delta))
        _smoothY = list.contentY
        _programmatic = false
    }

    // ================= the strip =================
    ListView {
        id: list
        anchors.fill: parent
        model: root.core ? root.core.stripModel : null
        orientation: ListView.Vertical
        cacheBuffer: Math.round(Math.max(1, root.height) * root.cacheScreens)
        boundsBehavior: Flickable.StopAtBounds
        interactive: false                    // wheel drives the smooth accumulator; drag/flick is Task 11
        pixelAligned: false                   // preserve the sub-pixel float feel

        delegate: Item {
            id: del
            width: ListView.view ? ListView.view.width : root.width
            height: model.displayHeight > 0 ? model.displayHeight : 0   // MODEL-authoritative geometry

            readonly property string errorText: (root.failedRev, root._failText(model.pageIndex, model.errorCode))
            readonly property bool hasError: errorText.length > 0
            readonly property alias imageSource: pageImg.source   // exposed so the decode-refresh test can see it re-evaluate
            readonly property alias previewSource: previewImg.source
            readonly property alias previewCap: previewImg.sourceSize.width
            readonly property alias hqCap: pageImg.sourceSize.width
            // The hq fade as a RULE, not as the animated number. A Behavior makes `opacity`
            // untestable — a synchronous read straight after a target flip still returns the OLD
            // value (measured on the Single surface, which carries the same note) — so the rule is
            // the named property and `opacity` merely follows it.
            readonly property bool hqShown: pageImg.status === Image.Ready || pageImg._hqEverReady

            // ---- TWO TIERS, stacked (Task 8), exactly as Single and Pair stack them ----
            // A fast half-width PREVIEW underneath, the real page faded over it as it completes.
            // The column is the surface that meets new pages continuously, so what a single-tier
            // strip cost was not one wait per page turn but one per page scrolled past: black band,
            // then the whole page at once. Geometry is unaffected — both tiers take their box from
            // the MODEL (see the delegate's height above), never from an Image's implicit size, so a
            // tier arriving can never reflow the column.

            // PREVIEW tier: first pixels on screen.
            Image {
                id: previewImg
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: model.displayWidth > 0 ? model.displayWidth : parent.width
                height: parent.height
                visible: !del.hasError
                source: (root.readyRev, (root.core && root.core.imageUrl)
                        ? root.core.imageUrl(model.pageIndex, "preview") : "")
                asynchronous: true
                cache: true
                retainWhileLoading: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: root.previewCapW
                mipmap: true
            }

            // HQ tier: the reader's real page, over the preview.
            Image {
                id: pageImg
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: model.displayWidth > 0 ? model.displayWidth : parent.width
                height: parent.height
                visible: !del.hasError
                // readyRev folded in so the binding re-runs (and re-fetches the bumped ?rev= url) on decode
                source: (root.readyRev, (root.core && root.core.imageUrl)
                        ? root.core.imageUrl(model.pageIndex, "hq") : "")
                asynchronous: true
                cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode, and
                               // WITHOUT the pixmap cache every delegate rebuild re-pays the provider's full-res
                               // SmoothTransformation downscale — the "scroll back up and it stutters" cost.
                retainWhileLoading: true      // hold the current pixels through a redecode (no flash)
                fillMode: Image.PreserveAspectFit
                sourceSize.width: root.srcCapW   // decode what it LOOKS like, not what it IS
                mipmap: true                     // it is still a downscale — without this it shimmers while scrolling
                // The SAME 90ms cross-fade the paged surfaces use, so a page sharpening reads the
                // same wherever you meet it. `_hqEverReady` latches per delegate: once this page's
                // hq has landed, a later redecode holds at 1 and lets retainWhileLoading show the
                // old pixels going soft rather than dimming the page (the Single surface's scar).
                property bool _hqEverReady: false
                onStatusChanged: if (status === Image.Ready) _hqEverReady = true
                opacity: del.hqShown ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
            }

            // typed error placard — this page only; the rest of the column keeps reading
            Rectangle {
                anchors.centerIn: parent
                visible: del.hasError
                width: Math.min(parent.width * 0.72, 360)
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
                        text: del.errorText
                        color: "#ff8a8a"
                        font.pixelSize: 14
                        font.family: "Segoe UI"
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Page " + (model.pageIndex + 1)
                        color: "#9a99a5"
                        font.pixelSize: 11
                        font.family: "Segoe UI"
                    }
                }
            }
        }

        onContentYChanged: {
            root._scheduleReport()
            if (!root._draining) root._smoothY = list.contentY   // resync on any external move
            // Tracking is provenance-BLIND: keyboard, scrub, wheel — a move is a move. Only
            // _programmatic writes (resume, compensation, layout anchoring) stay silent, so
            // mounting or restoring never clobbers the shell's page.
            if (!root._programmatic) root._scheduleEmit()
        }
        // A WIDTH change reflows the whole column, so it is reported SYNCHRONOUSLY — never through
        // the throttle. See _flushViewportReportNow: deferring this by even one frame is what made
        // Hemanth's fullscreen transition shake.
        onWidthChanged: root._flushViewportReportNow()
        onHeightChanged: root._scheduleReport()
    }

    // ================= throttled viewport report (<= once per frame) =================
    property bool _reportPending: false
    property int _lastReportedWidth: -1
    Timer { id: reportTimer; interval: 16; repeat: false; onTriggered: root._flushViewportReport() }
    function _scheduleReport() {
        // Both surfaces are mounted at once against the SAME core; a HIDDEN strip must not drive the
        // shared decode pool / cache. Only the active strip reports its viewport.
        if (!active) return
        _reportPending = true
        if (!reportTimer.running) reportTimer.start()
    }
    // Report NOW, in this same beat, bypassing the 16ms throttle.
    //
    // WHY (measured, tests/comicreader_fullscreen_timing_probe.qml, 2026-07-26). Every fullscreen
    // flip in the app goes through the shared FullscreenTransitionShield: cover fades in, the window
    // mode flips behind it, and the cover lifts on the window's very NEXT frameSwapped. That is
    // honest for Player 2 — one textured quad that letterboxes to the new size within that same
    // frame, which is exactly why Hemanth calls its transition the smooth one. The strip does not
    // settle in one frame: the width change rescales the entire page column and scales contentY to
    // hold the reading position. Routed through the throttle, that landed 2 frames after the cover
    // began lifting entering fullscreen and 5 frames after it leaving — the probe recorded contentY
    // jumping 12000 -> 17067 in full view. The settle was happening in FRONT of the cover instead of
    // behind it. That is the shake, and it is why the earlier decode-cap fix (a real but separate
    // cost) did not cure it.
    //
    // The throttle exists to coalesce SCROLL reports, which arrive at 60Hz while reading. A width
    // change is rare and must never be coalesced, so only the width path is made synchronous; the
    // scroll and height paths keep the throttle exactly as before.
    function _flushViewportReportNow() {
        if (!active) return
        reportTimer.stop()
        _flushViewportReport()
    }
    function _flushViewportReport() {
        _reportPending = false
        if (!active || !core) return
        if (core.setStripViewport) core.setStripViewport(list.contentY, list.height)
        _noteRange()      // the retention window rides the SAME door, on its own slower clock
        if (core.setStripViewportWidth && list.width !== _lastReportedWidth) {
            // A width change (fullscreen, window resize) rescales the page column. The backend does this
            // IN PLACE (dataChanged, not a model reset), so the ListView keeps its delegates and its
            // contentY and simply reflows — no blink, no jump to page 1. The whole column scaled by
            // `ratio`, so scale the (preserved) scroll by the same factor to hold the read position.
            // forceLayout settles the new contentHeight first so the scaled value isn't clamped short.
            // Skip the first report (_lastReportedWidth < 0): initial layout, resume owns the position.
            var ratio = (_lastReportedWidth > 0) ? (list.width / _lastReportedWidth) : 1
            _lastReportedWidth = list.width
            core.setStripViewportWidth(list.width)
            if (ratio > 0 && ratio !== 1 && list.contentY > 0) {
                _programmatic = true
                list.forceLayout()
                list.contentY = list.contentY * ratio
                _smoothY = list.contentY
                _programmatic = false
            }
        }
    }

    // ================= the RETENTION window: core.requestRange (Task 8) =================
    // Task 2 built the viewport-shaped retention window and left it with no production caller. This
    // is that caller, and it is written against the warning requestRange's own doc comment carries:
    //
    //   "a call whose clamped range DIFFERS from the last one walks both cache hashes under both
    //    mutexes and frees QImages ON THE CALLING (GUI) THREAD, and the decoded walk contends the
    //    same mutex every provider worker takes for every page fetch. ... debounce or send it on
    //    settle, and send PAGE indices, not pixels."
    //
    // This reader exists BECAUSE of a stutter whose root cause was a per-frame GUI-thread cascade.
    // Wiring a per-frame scroll signal straight to a bulk free on the GUI thread would be the same
    // shape again, so there are three separate brakes and each one is doing different work:
    //
    //   1. PAGE INDICES, NOT PIXELS. A range only changes when the viewport crosses a page
    //      boundary. At the default 78% width a page is roughly two viewport heights, so an
    //      ordinary read changes the range every few seconds, not every frame.
    //   2. A QML-SIDE MEMO (_rangeFirst/_rangeLast). An unchanged range never crosses the C++
    //      boundary at all. The core has its own identical early-out; this one means we do not even
    //      pay the invoke, which is what makes a 60Hz caller safe.
    //   3. A LEADING+TRAILING THROTTLE. The first move of a fling sweeps immediately (the window
    //      must not lag the reader), then everything inside the window coalesces and the LAST of it
    //      lands when the window closes. Nothing is dropped; a fast fling through fifty pages costs
    //      four sweeps a second instead of sixty. The timer only stays alive while movement
    //      continues — a closing window with nothing waiting simply stops.
    //
    // A settle-only debounce (restart on every move, fire on quiet) was the other candidate and is
    // WRONG here for one specific reason: Auto-scroll moves the column continuously for minutes, so
    // "quiet" never arrives and the window would never update for the entire run.
    property int rangeThrottleMs: 250
    property bool _rangePending: false
    property int _rangeFirst: -1
    property int _rangeLast: -1
    Timer { id: rangeThrottle; interval: root.rangeThrottleMs; repeat: false; onTriggered: root._flushRangeWindow() }

    // The visible run as MODEL INDICES, or null when the column cannot honestly answer. Null rather
    // than a guess: indexAt returns -1 for a position with no realized delegate (mid-relayout, or
    // straight after a resume jumped the column thousands of pixels), and telling the backend to
    // retain a window we invented would evict the pages actually on screen.
    function _visibleRange() {
        if (list.count <= 0) return null
        var lo = list.indexAt(list.width / 2, list.contentY + 1)
        var hi = list.indexAt(list.width / 2, list.contentY + list.height - 1)
        if (lo < 0 || hi < 0 || hi < lo) return null
        return { first: lo, last: hi }
    }
    // THE guard that matters is here, not on the callers: the throttle TIMER can fire after a
    // layout switch has already taken this strip off screen, and both reading surfaces share one
    // backend — a hidden strip sweeping the retention window would evict what the visible one is
    // showing. (_noteRange has no second copy of this test on purpose; its only caller,
    // _flushViewportReport, is already gated, and _flushRangeWindow arrives from a timer that is
    // not.)
    function _sendRange() {
        if (!active || !core || !core.requestRange) return
        var r = _visibleRange()
        if (!r) return
        if (r.first === _rangeFirst && r.last === _rangeLast) return   // brake 2: never pay the invoke twice
        _rangeFirst = r.first
        _rangeLast = r.last
        core.requestRange(r.first, r.last)
    }
    function _noteRange() {
        if (!core || !core.requestRange) return
        if (rangeThrottle.running) { _rangePending = true; return }
        _sendRange()
        rangeThrottle.start()
    }
    // CLOSE the window. The one door, whether the 250ms timer reached it or a caller wants the
    // window shut now — stopping the timer first means "the window is closed" is true on the way
    // out either way, which is what lets the gate drive this deterministically instead of sleeping.
    function _flushRangeWindow() {
        rangeThrottle.stop()              // a no-op when the (non-repeating) timer itself got here
        if (!_rangePending) return        // nothing waiting: stop, and leave no timer running
        _rangePending = false
        _sendRange()
        rangeThrottle.start()             // still moving: hold the window open
    }

    // ================= throttled tracking emit (<= once per ~80ms window) =================
    // Reader 1's pageTrack (MangaReader.qml, pre-cutover): "onContentYChanged: if
    // (!pageTrack.running) pageTrack.start()", a single-shot 80ms Timer. _emitUserScroll() does three
    // list.indexAt() probes and builds the visible-index array — per-frame during a glide that is
    // pure overhead on the one thread that has to hold 60fps, so it is scheduled, not called inline.
    property bool _emitPending: false
    Timer { id: emitTimer; interval: 80; repeat: false; onTriggered: root._flushEmit() }
    function _scheduleEmit() {
        _emitPending = true
        if (!emitTimer.running) emitTimer.start()
    }
    function _flushEmit() {
        _emitPending = false
        _emitUserScroll()
    }
    // presented(), on its own. Split out from _emitUserScroll because the two have DIFFERENT rules
    // about provenance: scrolled()/pageInView() are suppressed for a programmatic move, because they
    // write back into the shell's page/fraction and a restore that emitted them would clobber the very
    // spot it was restoring to. presented() writes back into nothing — it is an outbound notice that
    // the reader can now see this position — so a restore SHOULD emit it. Without that, the landing
    // page of a resumed book was never reported at all, and Task 11 banks progress on this signal.
    //
    // A page showing its typed failure placard COUNTS: the one rule all three surfaces now follow is that
    // presented() means "the reader can see this position's content, or an explicit account of why
    // not". Task 11 must not sit waiting to bank a position that can never render; whether to offer a
    // retry is a separate question from where the reader is.
    //
    // GATED ON `active`, like the other two surfaces' _notePresented(). An UNMOUNTED strip still holds
    // a laid-out column and still receives seekToPage/haltScrollAt from the shell across a layout
    // switch; reporting from there would tell Task 11 the reader saw a page that is not on screen.
    //
    // IT ASKS THE BACKEND, NOT THE DRAWN COLUMN, and that is the difference between a report that
    // works on a restore and one that does not. A resume or a layout switch jumps contentY thousands
    // of pixels in one write, and the ListView only holds items around where it WAS: measured
    // 2026-07-30, indexAt() and itemAtIndex() both answer nothing at the new position, and neither
    // forceLayout() nor a later event-loop pass brought them back in the offscreen harness. This is
    // the SAME trap seekToPage documents on the way in ("the ListView only realizes delegates near
    // the viewport, so a page you are switching TO has no y to read yet"), and the shell already
    // answers this exact question the same way (core.stripPageAtCenter, for the scrub bubble).
    // The drawn column stays as the FALLBACK, for a partial core that has no strip geometry seam.
    function _emitPresented() {
        if (!active) return
        if (list.count <= 0) return
        var centreY = list.contentY + list.height / 2
        var idx = -1
        var frac = -1
        if (core && core.stripPageAtCenter) {
            idx = core.stripPageAtCenter(list.contentY, list.height)
            if (idx >= 0 && core.stripPageHeight && core.stripPageTop) {
                var h = core.stripPageHeight(idx)
                if (h > 0) frac = (centreY - core.stripPageTop(idx)) / h
            }
        }
        if (idx < 0) {
            idx = list.indexAt(list.width / 2, centreY)
            if (idx < 0) return
        }
        if (frac < 0) {
            var it = list.itemAt(list.width / 2, centreY)
            frac = (it && it.height > 0) ? (centreY - it.y) / it.height : 0
        }
        root.presented(idx + 1, Math.max(0, Math.min(1, frac)))
    }
    // Change the strip's page width / gap WITHOUT losing the reader's place. The backend owns the
    // geometry (QML paints, C++ decides): it anchors the page under the viewport centre across the
    // relayout and hands back the top to land on. forceLayout settles the new contentHeight first,
    // so assigning that top is not clamped short against a stale one.
    function applyLayout(widthPct, gap) {
        if (!core || !core.setStripLayout) return
        var newTop = core.setStripLayout(widthPct, gap, list.contentY, list.height)
        // A partial seam (an older backend, a harness fake) returns nothing — hold the current
        // position rather than assigning NaN into contentY and blanking the strip.
        if (typeof newTop !== "number" || !isFinite(newTop)) newTop = list.contentY
        _programmatic = true
        list.forceLayout()
        var maxY = Math.max(0, list.contentHeight - list.height)
        list.contentY = Math.max(0, Math.min(maxY, newTop))
        _smoothY = list.contentY
        _programmatic = false
    }

    // Becoming active (mode switched back to strip) — push the current viewport once so the backend
    // resumes decoding the right window (geometry changes while hidden were intentionally ignored)...
    // ...and report the position the reader is now looking at. Mounting a surface onto a position the
    // column is already parked at IS a presentation, and it is the same rule the two paged surfaces
    // follow in their own onActiveChanged. Without it, switching layout back to Long Strip left the
    // landing page unreported until the reader happened to scroll.
    onActiveChanged: if (active) { _scheduleReport(); Qt.callLater(root._emitPresented) }

    // ================= smooth-wheel float accumulator (Tankoban-Max / QTGW SmoothScrollArea) =================
    property real _pendingWheelPx: 0         // bounded backlog of intake still to drain
    property real _smoothY: 0                // float sub-pixel scroll position — contentY IS this
    property bool _drainFresh: false         // first tick after an idle start
    readonly property real _drainFraction: 0.38   // TB2: 38% of the backlog per frame
    readonly property real _maxBacklogPx: 6000    // a wheel flurry can't queue forever

    // The drain is a FrameAnimation, NOT a Timer. It is the single biggest thing separating smooth
    // from harsh: a FrameAnimation ticks ON the render loop, so each step lands exactly once per
    // presented frame. A 16ms Timer free-runs against the compositor instead — it beats with the
    // vsync, so steps double up or drop out and the column judders no matter how good the easing is.
    FrameAnimation {
        id: scrollDrain
        running: false
        onTriggered: root._drainWheel()
    }

    // 1.4 px per angle unit = 168px/notch — the house tuning from the reader this one
    // replaced, restored on Hemanth's ruling (2026-07-25). We had TB2's 100px/notch: the
    // same easing curve travelling 40% less per notch, which reads as smooth-but-heavy.
    // The trackpad path is untouched — pixelDelta is already real pixels on every platform.
    function _intakeWheel(angleY, pixelY) {
        var dy = pixelY
        if (dy === 0) dy = angleY * 1.4
        if (dy === 0) return
        // WHEEL-ONLY provenance, fired before the shared glide call below: manualNavigation() means
        // "a real mouse gesture happened", which a keyboard glide must never forge.
        _userInteracted = true
        manualNavigation()
        smoothScrollBy(-dy)                                 // wheel-down -> +contentY
    }

    // Glide by px through the SAME accumulator the wheel uses, so a keyboard press feels like one
    // big notch rather than a teleport.
    //
    // WHY THIS IS SHARED (E2). A raw `contentY = ...` write bypasses both the glide and the backlog.
    // Press Space in the middle of a wheel glide and the old code jumped you instantly AND THEN kept
    // sliding on the leftover wheel input still in the drain — jump-then-slide, the exact tell of two
    // scroll systems fighting. Reader 1 routes keys through smoothScrollBy and pins instant moves
    // through haltScrollAt; this restores that discipline. Instant, final repositions (a scrub seek,
    // Home/End) still go through haltScrollAt, which pins the position AND drops the in-flight
    // backlog so nothing carries across the jump.
    function smoothScrollBy(px) {
        if (px === 0) return
        // Starting from idle: re-anchor on the real position and mark this drain FRESH.
        if (!scrollDrain.running) { _smoothY = list.contentY; _drainFresh = true }
        _pendingWheelPx = Math.max(-_maxBacklogPx, Math.min(_maxBacklogPx, _pendingWheelPx + px))
        if (!scrollDrain.running) scrollDrain.running = true
    }

    // Drain 38% of the remaining backlog per frame — but scaled by how much time ACTUALLY passed, so
    // the same glide feels identical at 60Hz, 120Hz or through a frame hitch. A fixed 38%-per-tick
    // silently becomes a different curve on every other refresh rate.
    function _drainWheel() {
        if (Math.abs(_pendingWheelPx) < 0.75) {
            _pendingWheelPx = 0
            scrollDrain.running = false
            return
        }
        // re-anchor if something else moved the view (a drag, a resume, a seek)
        if (Math.abs(list.contentY - _smoothY) > 1.5) _smoothY = list.contentY

        var frames = Math.min(3, Math.max(0.25, scrollDrain.frameTime * 60))
        // COLD-START (measured 2026-07-17, agents/wheel_latency_harness.qml): the first tick after
        // idle reports a ~3ms frameTime, so the 0.25 floor drained only 11% — about 19px, below the
        // threshold you can see. Every fresh scroll began with a dead beat. The first tick always
        // drains the full fraction. [[reference_frameanimation_cold_first_tick_underdrains]]
        if (_drainFresh) { frames = Math.max(1, frames); _drainFresh = false }

        var take = _pendingWheelPx * (1 - Math.pow(1 - _drainFraction, frames))
        if (Math.abs(_pendingWheelPx) <= 1) take = _pendingWheelPx    // final settle
        var maxY = Math.max(0, list.contentHeight - list.height)
        var y = _smoothY + take
        if (y <= 0 || y >= maxY) {          // ran into an edge: clamp and drop the backlog
            y = Math.max(0, Math.min(maxY, y))
            _pendingWheelPx = 0
        } else {
            _pendingWheelPx -= take
        }
        _smoothY = y
        // Assign the FLOAT, never Math.round(): rounding quantises every step to whole pixels, which
        // is exactly what "harsh" feels like at the slow end of a glide, where the true step is a
        // fraction of a pixel per frame and rounding turns it into stand-still-then-jump.
        _draining = true
        list.contentY = y
        _draining = false
        if (_pendingWheelPx === 0) scrollDrain.running = false
    }

    // ================= AUTO-SCROLL drive (Task 8) =================
    // A SECOND FrameAnimation, deliberately not the wheel drain. They are different machines: the
    // drain empties a backlog and stops, auto-scroll travels at a constant rate until something
    // stops it. Folding auto-scroll into the drain would mean continuously refilling _pendingWheelPx
    // to keep it alive, which would also make every wheel notch fight the motion inside one
    // accumulator. Two drives, one rule between them: manual input pauses auto-scroll BEFORE it
    // applies its own movement (see _intakeWheel), so they are never both writing contentY.
    //
    // `active` is in the running condition because both reading surfaces are mounted at once: a
    // HIDDEN strip must not keep scrolling a column nobody is looking at.
    property bool _autoScrollFresh: false
    FrameAnimation {
        id: autoScrollDrive
        running: root.autoScrollRunning && root.active
        onRunningChanged: if (running) root._autoScrollFresh = true
        onTriggered: root._autoScrollTick(autoScrollDrive.frameTime * 1000)
    }

    // ONE tick of motion, in milliseconds. Named and public so the gate can drive it deterministically
    // — an offscreen harness never ticks a FrameAnimation, and a test that slept for real frames
    // would be a flake.
    //
    // IT WRITES contentY AND NOTHING ELSE. No width, no gap, no layout call: that is the
    // never-resize rule made structural rather than guarded.
    function _autoScrollTick(ms) {
        if (list.count <= 0) return
        var dt = Number(ms)
        if (!isFinite(dt) || dt <= 0) return
        // COLD FIRST TICK. FrameAnimation reports a ~3ms frameTime on the tick right after it
        // starts (measured 2026-07-17, agents/wheel_latency_harness.qml) — the same trap the wheel
        // drain documents a few hundred lines above, fixed once already in c307ccb. Left alone the
        // first step is a fifth of a frame's travel, which reads as the page hesitating before it
        // moves. The first tick is worth a whole frame.
        // [[reference_frameanimation_cold_first_tick_underdrains]]
        if (_autoScrollFresh) { dt = Math.max(dt, 1000 / 60); _autoScrollFresh = false }
        // ...and the opposite guard: a stalled frame (a decode burst, a window drag) must not
        // teleport the page half a chapter down when the clock resumes.
        dt = Math.min(dt, 100)

        var maxY = Math.max(0, list.contentHeight - list.height)
        if (maxY <= 0) { root.autoScrollEnded(); return }   // nothing to travel: stop, don't spin

        var y = list.contentY + autoScrollPixelsPerSecond * dt / 1000
        var hitEnd = y >= maxY
        if (hitEnd) y = maxY

        // NOT _programmatic, and that is deliberate: auto-scroll is REAL READING. Suppressing the
        // tracking pair here would leave the HUD counter and the Continue record sitting on the
        // page you started from while the book moved underneath them.
        list.contentY = y
        _smoothY = y
        if (hitEnd) root.autoScrollEnded()                  // the end of the book stops the motion
    }

    // Land page `page0` (0-based) at the top of the viewport. The top comes from the BACKEND, not
    // from a delegate: the ListView only realizes delegates near the viewport, so a page you are
    // switching TO has no y to read yet — which is exactly why an immediate seek lands at 0.
    function seekToPage(page0) {
        if (!core || !core.stripPageTop) return false
        var span = list.contentHeight - list.height
        if (span <= 0) return false            // not laid out yet, or the whole book fits
        haltScrollAt(Math.max(0, Math.min(span, core.stripPageTop(page0))))
        return true
    }

    // Stop any glide and pin the view at y — every instant reposition goes through here, so a seek
    // can never be fought by a drain that is still carrying old backlog.
    function haltScrollAt(y) {
        scrollDrain.running = false
        _pendingWheelPx = 0
        _smoothY = y
        _programmatic = true
        list.contentY = y
        _programmatic = false
        // The reader is now looking at wherever this put them, so report it — presented() only, never
        // the tracking pair (see _emitPresented). Deferred through Qt.callLater so it lands after the
        // caller's own bindings settle; callLater collapses repeats within one pass (measured), so a
        // seekToPage — which comes through here — reports once, not twice.
        Qt.callLater(root._emitPresented)
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) { root._intakeWheel(event.angleDelta.y, event.pixelDelta.y) }
    }

    // ================= user-scroll reporting =================
    // NOTE: the surface no longer restores anything itself. Resume is a one-shot COMMAND from the
    // shell (seekToPage / haltScrollAt above), never a bound `resumeFraction` the surface re-applies:
    // the shell's fraction is written BY this surface's own onScrolled, so a binding the surface acts
    // on is a scroll -> fraction -> apply -> scroll loop, and the latch that used to break the loop
    // was per-object-lifetime — the reader is a persistent child, so the second book never resumed.
    function _emitUserScroll() {
        if (list.count <= 0) return
        var span = list.contentHeight - list.height
        var frac = span > 0 ? (list.contentY / span) : 0
        root.scrolled(Math.max(0, Math.min(1, frac)))
        var centreY = list.contentY + list.height / 2
        var idx = list.indexAt(list.width / 2, centreY)
        if (idx >= 0)
            root.pageInView(idx + 1)               // 1-based (shell currentPage scale)
        _emitPresented()
        var lo = list.indexAt(list.width / 2, list.contentY + 1)
        var hi = list.indexAt(list.width / 2, list.contentY + list.height - 1)
        if (lo >= 0 && hi >= 0) {
            var arr = []
            for (var i = lo; i <= hi; i++) arr.push(i)
            root.visiblePages(arr)
        }
    }
}

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

Item {
    id: root

    // ---- injectable seam + inputs (bound by the shell) ----
    property var core: null
    property bool active: true
    property bool rtl: false                 // strip is vertical; kept for parity / future affordances
    property int cacheScreens: 2             // cacheBuffer in viewport-heights (modest -> virtualized)

    // ---- DECODE CAP (the lineage's sourceSize.width; the strip had none) ----
    // Without a cap, every page decodes and uploads its FULL source resolution — a scanned tankobon
    // page is routinely 2000-3000px wide — while it is displayed at a fraction of that. The cost is
    // paid on the render thread, once per page arriving on screen, which is why an uncapped strip
    // reads as smooth-but-intermittently-laggy rather than uniformly harsh: you only feel it when a
    // fresh page comes into view. The reader this one replaced capped its strip at 1100px.
    //
    // DELIBERATE DEVIATION from that flat 1100: it would be visibly soft on a large display, where
    // a page is shown wider than that. This caps to the VIEWPORT width instead — never smaller than
    // the lineage's 1100, never more than 2048 — so it can't be soft and can't be wasteful.
    // Quantised to 256px steps, and taken from the viewport rather than the page's own width, so
    // neither a drag-resize nor a Page-width chip re-decodes the whole column over a few pixels.
    readonly property int srcCapW:
        Math.max(1100, Math.min(2048, Math.ceil(Math.max(320, width) / 256) * 256))

    // ---- outputs consumed by the shell / HUD (Task 11) ----
    // These three are PROVENANCE-BLIND: they fire for any non-programmatic move — wheel, keyboard,
    // scrub drag, Home/End. Only _programmatic writes (resume, compensation, layout anchoring) stay
    // silent. Gating them on "was it a wheel gesture" is what made reading with the spacebar file a
    // Continue record of page 1.
    signal pageInView(int page)              // 1-based page at the vertical center
    signal visiblePages(var indices)         // 0-based indices currently on screen
    signal scrolled(real fraction)           // 0..1 scroll fraction
    // Unlike the three above this one IS wheel-only — it means "a real gesture happened", not
    // "the position changed". NOTE: currently has no consumer; kept for a HUD/record consumer that
    // needs to tell a gesture from a programmatic move. Delete it if that never arrives.
    signal manualNavigation()

    clip: true

    // full-bleed black stage — the page owns the screen
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- test/observation surface (harness reads these; also handy for the HUD later) ----
    property alias contentY: list.contentY
    readonly property int rowCount: list.count
    readonly property real contentHeight: list.contentHeight
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
        function onStripCompensation(delta) { root._applyCompensation(delta) }
    }

    function _applyCompensation(delta) {
        // a page above the fold changed height — shift our scroll position by the same delta so the
        // read position holds. Programmatic (never a user gesture).
        _programmatic = true
        list.contentY = list.contentY + delta
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

            // the page image — geometry comes from the delegate (model), NOT the image's implicit size
            Image {
                id: pageImg
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: model.displayWidth > 0 ? model.displayWidth : parent.width
                height: parent.height
                visible: !del.hasError
                // readyRev folded in so the binding re-runs (and re-fetches the bumped ?rev= url) on decode
                source: (root.readyRev, (root.core && root.core.imageUrl) ? root.core.imageUrl(model.pageIndex) : "")
                asynchronous: true
                cache: false                  // the ?rev= in the url busts QML's cache on redecode
                retainWhileLoading: true      // hold the current pixels through a redecode (no flash)
                fillMode: Image.PreserveAspectFit
                sourceSize.width: root.srcCapW   // decode what it LOOKS like, not what it IS
                mipmap: true                     // it is still a downscale — without this it shimmers while scrolling
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
        onWidthChanged: root._scheduleReport()
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
    function _flushViewportReport() {
        _reportPending = false
        if (!active || !core) return
        if (core.setStripViewport) core.setStripViewport(list.contentY, list.height)
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

    // becoming active (mode switched back to strip) — push the current viewport once so the backend
    // resumes decoding the right window (geometry changes while hidden were intentionally ignored).
    onActiveChanged: if (active) _scheduleReport()

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
        _userInteracted = true
        manualNavigation()
        // Starting from idle: re-anchor on the real position and mark this drain FRESH.
        if (!scrollDrain.running) { _smoothY = list.contentY; _drainFresh = true }
        _pendingWheelPx = Math.max(-_maxBacklogPx,
                          Math.min(_maxBacklogPx, _pendingWheelPx - dy))   // wheel-down -> +contentY
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
        var idx = list.indexAt(list.width / 2, list.contentY + list.height / 2)
        if (idx >= 0) root.pageInView(idx + 1)     // 1-based (shell currentPage scale)
        var lo = list.indexAt(list.width / 2, list.contentY + 1)
        var hi = list.indexAt(list.width / 2, list.contentY + list.height - 1)
        if (lo >= 0 && hi >= 0) {
            var arr = []
            for (var i = lo; i <= hi; i++) arr.push(i)
            root.visiblePages(arr)
        }
    }
}

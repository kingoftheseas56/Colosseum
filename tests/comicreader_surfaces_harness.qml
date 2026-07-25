// Comic Reader — READING SURFACES oracle (Task 10).
//
// Instantiates qml/comicreader/ComicReaderStripSurface.qml and
// qml/comicreader/ComicReaderDoubleSurface.qml offscreen with an INJECTED FAKE core (the
// ComicReaderCore QML-facing API from Task 7) and asserts the geometry/direction/feel logic the
// two surfaces own. The pixel look is Hemanth's eyes-on later (Task 14); this pins BEHAVIOR:
//
//   STRIP (ComicReaderStripSurface):
//     * ListView over core.stripModel VIRTUALIZES — a delegate exists for the near-viewport window
//       (itemAt(0) != null) but NOT for a far page (itemAt(190) == null) in a 200-row model.
//     * delegate HEIGHT comes from the model role displayHeight (geometry is authoritative): change
//       a row's displayHeight -> the delegate's height changes (NOT the loaded Image's implicit
//       size — offscreen the Image never loads, so an Image-driven height would be 0).
//     * viewport report to core.setStripViewport(top,height) is THROTTLED/COALESCED — N synchronous
//       contentY ticks in one frame produce ZERO immediate calls (a pending flush), and exactly ONE
//       call after the frame flush, carrying the LATEST viewport.
//     * core.stripCompensation(delta) -> contentY shifts by delta (anti-jump).
//     * the smooth-wheel float accumulator (Tankoban-Max / QTGW SmoothScrollArea): a wheel intake
//       lands ~100px/notch into a bounded backlog, and each 16ms drain takes ~0.38 of the backlog
//       clamped to a max step (no giant jump), draining smoothly in float sub-pixel space.
//     * a per-page pageFailed(page,code) shows the typed placard for THAT page's delegate only.
//
//   DOUBLE (ComicReaderDoubleSurface):
//     * a real PAIR unit renders TWO images; RTL vs LTR FLIPS the physical x-order of the
//       rightIndex/leftIndex pages (RTL: rightIndex on the physical right).
//     * a spread / coverAlone / single (leftIndex<0) unit renders ONE full-width image (no second
//       image, no fabricated crop), and shows NO gutter shadow.
//     * a real pair shows the gutter shadow element; a spread does not.
//     * zoom clamps to [100,260]; pan clamps to [0,panXMax]/[0,panYMax]; a unit change RESETS
//       pan only — zoom SURVIVES a page turn (a magnified volume must stay readable).
//     * the maxSeen pair-anchor contract (shell Task 9, onCurrentPageChanged): showing a unit emits
//       unitShown(highestPage) with the reading-HIGHEST page of the unit (max(rightIndex,leftIndex),
//       1-based) so a pair-terminated entry can reach `finished`.
//
// The fake core exposes the exact API surface the surfaces touch: stripModel (a QML ListModel with
// the Task-6 roles), imageUrl(p), unitForPage(p), pageInfo(p), setVisible/setStripViewport/
// setStripViewportWidth spies, and the pageReady/pageFailed/stripCompensation signals. Every
// `core.` use in the surfaces is guarded so a null/partial core never errors (the reason they also
// survive the shell's Task-9 fake, which lacks imageUrl/stripModel/setStripViewport).
//
// HOUSE HARNESS PATTERN (mirrors comicreader_shell_harness.qml): a thrown error HANGS qml.exe
// offscreen, so `ck` never throws — it collects failures; the run prints exactly ONE
// COMICREADER_SURFACES_OK when clean, else one COMICREADER_SURFACES_FAIL:<msg> per failure and
// Qt.exit(1). Because the viewport report is throttled behind a frame flush, there is a SYNC phase
// (all immediate assertions, driven by the surfaces' test-visible flush hook) — no wall-clock wait
// is needed because _flushViewportReport() is invoked directly (the same entry point the 16ms
// timer calls). A safety-net Timer fails loudly on a true hang instead of stalling CI.

import QtQuick

Item {
    id: harness
    width: 900; height: 600
    visible: true            // offscreen platform: needed so the ListView lays out + creates delegates

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }
    function approx(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-6 : eps) }

    // ---- fake backend core: the ComicReaderCore QML-facing API (Task 7) the surfaces drive ----
    component FakeCore: QtObject {
        property var stripModel: null            // a ListModel instance, assigned by the scenario
        // unit config: 0-based page -> unit map; default = single (rightIndex=page)
        property var units: ({})
        // spies
        property int setVisibleCalls: 0
        property var lastVisible: []
        property int setStripViewportCalls: 0
        property real lastViewportTop: -1
        property real lastViewportHeight: -1
        property int setStripViewportWidthCalls: 0
        property int lastViewportWidth: -1
        // per-page rev — mirrors the C++ m_pageRev bumped on (re)decode. Stored as a sub-key of a
        // `var` object so mutating it does NOT emit a QML change (exactly like the C++ member: the
        // surfaces' `source` binding CANNOT see it change). imageUrl only READS it (no write inside
        // the binding -> no binding loop). The ONLY thing that re-drives `source` is the surface's
        // own readyRev counter — which is the fix under test.
        property var pageRevs: ({})
        // whether an entry is loaded — before openEntry, unitForPage returns a degenerate empty unit
        // (mirrors the real core with no entry). loadEntry() flips it + fires the load signals.
        property bool loaded: true
        // signals (shape parity with the real core)
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal stripCompensation(real delta)
        signal entryChanged()      // real core emits this on openEntry (ComicReaderCore.cpp:201)
        signal pairingChanged()    // ...and this (ComicReaderCore.cpp:202), + on every rebuildUnits
        function loadEntry() { loaded = true; entryChanged(); pairingChanged() }
        function imageUrl(page) {
            var r = (pageRevs[page] !== undefined) ? pageRevs[page] : 0
            return "image://comicreader/1/" + page + "?rev=" + r
        }
        // a decode landed: bump the (invisible) rev, THEN fire pageReady — like the real core.
        function emitPageReady(page) {
            pageRevs[page] = ((pageRevs[page] !== undefined) ? pageRevs[page] : 0) + 1
            pageReady(page)
        }
        function unitForPage(page) {
            if (!loaded) return { rightIndex: -1, leftIndex: -1, spread: false, coverAlone: false }
            if (units[page] !== undefined) return units[page]
            return { rightIndex: page, leftIndex: -1, spread: false, coverAlone: false }
        }
        function pageInfo(page) { return { error: "" } }
        function setVisible(pages) { setVisibleCalls += 1; lastVisible = pages }
        function setStripViewport(top, height) {
            setStripViewportCalls += 1; lastViewportTop = top; lastViewportHeight = height
        }
        function setStripViewportWidth(w) { setStripViewportWidthCalls += 1; lastViewportWidth = w }
    }

    // a strip model (Task-6 roles: pageIndex/top/displayWidth/displayHeight/ready/errorCode)
    ListModel { id: stripModelA }
    ListModel { id: stripModelFail }

    FakeCore { id: coreStrip }
    FakeCore { id: coreFail }
    FakeCore { id: coreDouble }
    FakeCore { id: coreFresh }

    property var stripComp: null
    property var doubleComp: null
    property var stripSurface: null
    property var stripFailSurface: null
    property var doubleSurface: null

    // ---- unitShown capture ----
    property int capturedHighest: -1
    property int unitShownCount: 0

    function fillStripModel(m, n) {
        m.clear()
        for (var i = 0; i < n; i++)
            m.append({ pageIndex: i, top: i * 1220, displayWidth: 800, displayHeight: 1200,
                       ready: true, errorCode: 0 })
    }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_SURFACES_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_SURFACES_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    // ============================ STRIP ============================
    function runStrip() {
        // 200 tall rows -> content far exceeds viewport so virtualization is observable.
        fillStripModel(stripModelA, 200)
        coreStrip.stripModel = stripModelA

        stripSurface = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreStrip
        })
        if (!stripSurface) { failures.push("strip: createObject returned null"); return }
        stripSurface.forceRelayout()

        // --- virtualization: near window has a delegate, a far page does not ---
        ck(stripSurface.rowCount === 200, "strip: rowCount must be 200, got " + stripSurface.rowCount)
        ck(stripSurface.itemAt(0) !== null, "strip: delegate for page 0 (in window) must exist")
        ck(stripSurface.itemAt(190) === null, "strip: delegate for a far page (190) must NOT be created (virtualized)")

        // --- delegate height comes from the MODEL role, not the (never-loaded) Image ---
        var d0 = stripSurface.itemAt(0)
        ck(d0 !== null && approx(d0.height, 1200), "strip: delegate height must equal model displayHeight 1200, got " + (d0 ? d0.height : "<null>"))
        stripModelA.setProperty(0, "displayHeight", 1700)
        stripSurface.forceRelayout()
        d0 = stripSurface.itemAt(0)
        ck(d0 !== null && approx(d0.height, 1700), "strip: changing the model row's displayHeight must change the delegate height to 1700, got " + (d0 ? d0.height : "<null>"))

        // --- DECODE REFRESH (regression guard for C1): a not-yet-decoded page renders blank; when the
        // decode lands, pageReady must re-drive the delegate's `source` so the fresh ?rev= url is
        // re-fetched. Prove BOTH the readyRev dependency bumps AND the source actually re-evaluates. ---
        d0 = stripSurface.itemAt(0)
        var rr0 = stripSurface.readyRev
        var srcBefore = String(d0.imageSource)
        ck(srcBefore.indexOf("rev=0") >= 0, "strip: page 0's source must start at ?rev=0 before decode, got '" + srcBefore + "'")
        coreStrip.emitPageReady(0)                 // decode of page 0 lands (rev bumps C++-side)
        ck(stripSurface.readyRev === rr0 + 1, "strip: pageReady must bump readyRev (the source-refresh dependency), got " + stripSurface.readyRev)
        var srcAfter = String(stripSurface.itemAt(0).imageSource)
        ck(srcAfter.indexOf("rev=1") >= 0, "strip: after pageReady the delegate source must re-evaluate to the bumped ?rev=1 (page would stay BLANK otherwise), got '" + srcAfter + "'")
        ck(srcAfter !== srcBefore, "strip: the decode-refresh must actually change the delegate source (was '" + srcBefore + "')")

        // --- viewport report is THROTTLED: N synchronous ticks -> 0 immediate calls, 1 after flush ---
        coreStrip.setStripViewportCalls = 0
        coreStrip.lastViewportTop = -1
        stripSurface.contentY = 400
        stripSurface.contentY = 900
        stripSurface.contentY = 1500          // three ticks in one frame
        ck(coreStrip.setStripViewportCalls === 0, "strip: viewport report must be COALESCED (0 immediate calls for a burst), got " + coreStrip.setStripViewportCalls)
        ck(stripSurface._reportPending === true, "strip: a burst must leave a pending viewport report")
        stripSurface._flushViewportReport()   // the same entry point the 16ms timer calls
        ck(coreStrip.setStripViewportCalls === 1, "strip: exactly ONE viewport report after the frame flush, got " + coreStrip.setStripViewportCalls)
        ck(approx(coreStrip.lastViewportTop, 1500), "strip: the coalesced report must carry the LATEST contentY 1500, got " + coreStrip.lastViewportTop)
        ck(approx(coreStrip.lastViewportHeight, 480), "strip: the report must carry the viewport height 480, got " + coreStrip.lastViewportHeight)
        ck(coreStrip.setStripViewportWidthCalls >= 1 && coreStrip.lastViewportWidth === 520,
           "strip: the viewport WIDTH must be reported once (520), got calls=" + coreStrip.setStripViewportWidthCalls + " w=" + coreStrip.lastViewportWidth)

        // --- anti-jump: stripCompensation(delta) shifts contentY by delta ---
        stripSurface.contentY = 2000
        coreStrip.stripCompensation(60)
        ck(approx(stripSurface.contentY, 2060), "strip: stripCompensation(60) must add 60 to contentY (2000 -> 2060), got " + stripSurface.contentY)
        coreStrip.stripCompensation(-25)
        ck(approx(stripSurface.contentY, 2035), "strip: stripCompensation(-25) must subtract 25 (2060 -> 2035), got " + stripSurface.contentY)

        // --- smooth-wheel float accumulator ---
        // reset to a clean scroll state
        stripSurface.contentY = 0
        stripSurface._pendingWheelPx = 0
        stripSurface._smoothY = 0
        // 3 notches DOWN: angleDelta -360 -> intake ~ -360*100/120 = -300 -> +300 into the backlog
        stripSurface._intakeWheel(-360, 0)
        var pend0 = stripSurface._pendingWheelPx
        ck(approx(pend0, 300, 1.0), "wheel: 3-notch intake (~100px/notch) must land ~300px in the backlog, got " + pend0)
        // The drain model is PORTED from the reader this one replaced, not re-derived, so these
        // assertions pin the lineage's curve rather than a plausible-looking one:
        //   take = pending * (1 - (1-0.38)^frames),  frames = min(3, max(0.25, frameTime*60))
        // There is deliberately NO max-step clamp — an earlier pass here invented one, and it
        // throttled exactly the fast flings that are supposed to feel weightless.

        // COLD START: an intake from idle marks the drain FRESH, and a fresh tick always drains the
        // full fraction. Without it the first tick after idle reports a ~3ms frameTime, drains ~11%
        // (about 19px, below what you can see) and every scroll opens with a dead beat.
        ck(stripSurface._drainFresh === true, "wheel: an intake from idle must mark the drain FRESH (cold-start fix)")
        stripSurface._drainWheel()
        var moved1 = stripSurface._smoothY            // started at 0
        var expectTake1 = pend0 * 0.38                // frames == 1 on a fresh tick
        ck(approx(moved1, expectTake1, 0.5), "wheel: the first drain must take the FULL 0.38 of the backlog (~"
           + expectTake1.toFixed(2) + "), got " + moved1.toFixed(2))
        ck(stripSurface._drainFresh === false, "wheel: the cold-start fraction must be spent once, not every tick")
        ck(stripSurface._pendingWheelPx < pend0, "wheel: the backlog must drain down after a tick ("
           + pend0.toFixed(1) + " -> " + stripSurface._pendingWheelPx.toFixed(1) + ")")

        // drain to completion — monotone, decelerating, backlog -> 0
        var prevPend = stripSurface._pendingWheelPx
        var prevSmooth = stripSurface._smoothY
        var monotone = true, decelerating = true, guard = 0
        var lastStep = moved1
        var midSmooth = -1
        while (Math.abs(stripSurface._pendingWheelPx) >= 0.75 && guard < 200) {
            var pendBefore = stripSurface._pendingWheelPx
            stripSurface._drainWheel()
            var step = stripSurface._smoothY - prevSmooth
            if (step < -1e-6) monotone = false
            // An exponential drain never accelerates MID-GLIDE. The exception is the deliberate
            // final settle: once the backlog drops under 1px the drain takes the whole remainder in
            // one go rather than chasing an ever-halving sliver forever, so that last step is
            // legitimately larger than the one before it. Measured, not assumed - instrumenting the
            // sequence is what showed this step was the code behaving, and the assertion misreading it.
            if (Math.abs(pendBefore) > 1 && step > lastStep + 1e-6) decelerating = false
            if (stripSurface._pendingWheelPx > prevPend + 1e-6) monotone = false
            if (midSmooth < 0) midSmooth = stripSurface._smoothY   // sample one step into the glide
            lastStep = step
            prevPend = stripSurface._pendingWheelPx
            prevSmooth = stripSurface._smoothY
            guard += 1
        }
        ck(monotone, "wheel: the drain must be positional/monotone (backlog shrinks, smoothY advances)")
        ck(decelerating, "wheel: the glide must DECELERATE — an exponential drain never speeds up mid-flight")
        ck(approx(stripSurface._pendingWheelPx, 0, 0.75), "wheel: the backlog must fully drain to ~0, got " + stripSurface._pendingWheelPx)
        ck(stripSurface._smoothY > 0, "wheel: the accumulated scroll must be positive (scrolled down), got " + stripSurface._smoothY)

        // SUB-PIXEL: contentY carries the float accumulator itself, never a rounded copy. Rounding
        // quantises the slow tail of every glide into stand-still-then-jump, which is the single
        // most obvious "harsh" tell.
        // Sampled MID-GLIDE, not at rest: a completed glide lands on exactly the distance that was
        // put in (300px here), which is a whole number by construction and would pass even if every
        // step had been rounded.
        ck(Math.abs(midSmooth - Math.round(midSmooth)) > 1e-6,
           "wheel: _smoothY must accumulate in FLOAT sub-pixel space, got integer " + midSmooth)
        ck(Math.abs(stripSurface.contentY - stripSurface._smoothY) < 1e-9,
           "wheel: contentY must BE the float accumulator, not a rounded copy (contentY "
           + stripSurface.contentY + " vs smoothY " + stripSurface._smoothY + ")")

        // haltScrollAt pins the view and kills any backlog still in flight, so a seek can never be
        // fought by a glide that is still carrying old input.
        stripSurface._intakeWheel(-360, 0)
        stripSurface.haltScrollAt(1234.5)
        ck(approx(stripSurface.contentY, 1234.5, 0.001), "wheel: haltScrollAt must pin contentY exactly, got " + stripSurface.contentY)
        ck(stripSurface._pendingWheelPx === 0, "wheel: haltScrollAt must drop the in-flight backlog, got " + stripSurface._pendingWheelPx)

        // --- I2: a HIDDEN (inactive) strip must NOT report its viewport — both surfaces share one
        // core/pool, so a hidden strip driving decode requests would compete with the active surface. ---
        stripSurface.active = false
        coreStrip.setStripViewportCalls = 0
        stripSurface.contentY = 3000
        stripSurface._flushViewportReport()
        ck(coreStrip.setStripViewportCalls === 0, "strip: an INACTIVE strip must NOT report its viewport, got " + coreStrip.setStripViewportCalls + " calls")
        stripSurface.active = true
    }

    // ---- page failure isolation (its own surface + model so it never disturbs the strip asserts) ----
    function runStripFailure() {
        fillStripModel(stripModelFail, 8)
        coreFail.stripModel = stripModelFail
        stripFailSurface = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreFail
        })
        if (!stripFailSurface) { failures.push("strip-fail: createObject returned null"); return }
        stripFailSurface.forceRelayout()

        var before = stripFailSurface.itemAt(1)
        ck(before !== null, "strip-fail: page 1's delegate must exist (small in-window model)")
        ck(before !== null && before.hasError === false, "strip-fail: page 1 must have no error before pageFailed")

        coreFail.pageFailed(1, "missing_file")
        var d1 = stripFailSurface.itemAt(1)
        var d0 = stripFailSurface.itemAt(0)
        ck(d1 !== null && d1.hasError === true, "strip-fail: pageFailed(1) must show page 1's typed placard")
        ck(d1 !== null && d1.errorText.length > 0, "strip-fail: page 1's placard must carry typed text, got '" + (d1 ? d1.errorText : "<null>") + "'")
        ck(d0 !== null && d0.hasError === false, "strip-fail: page 0 must be UNAFFECTED by page 1's failure")

        coreFail.pageReady(1)                 // a later successful (re)decode clears the placard
        d1 = stripFailSurface.itemAt(1)
        ck(d1 !== null && d1.hasError === false, "strip-fail: pageReady(1) after a failure must clear page 1's placard")
    }

    // ============================ DOUBLE ============================
    function makeDouble(cfg) {
        var base = { "width": 800, "height": 480, "active": true, "core": coreDouble }
        for (var k in cfg) base[k] = cfg[k]
        return doubleComp.createObject(harness, base)
    }

    function runDouble() {
        // --- a real PAIR renders TWO images; RTL vs LTR flips the physical x-order ---
        coreDouble.units = { 3: { rightIndex: 3, leftIndex: 4, spread: false, coverAlone: false } }
        var dbl = makeDouble({ "currentPage": 4, "rtl": true })   // anchor page (1-based 4 => 0-based 3)
        if (!dbl) { failures.push("double: createObject returned null"); return }
        doubleSurface = dbl

        ck(dbl.isPair === true, "double: a two-index unit must be a PAIR")
        ck(dbl.imageCount === 2, "double: a pair must render TWO images, got " + dbl.imageCount)
        // RTL: the rightIndex page sits on the physical RIGHT (greater x) than the leftIndex page
        ck(dbl.rightIndexX > dbl.leftIndexX, "double RTL: rightIndex page must be physically RIGHT of leftIndex (rx=" + dbl.rightIndexX + " lx=" + dbl.leftIndexX + ")")
        dbl.rtl = false
        // LTR: mirrored — the rightIndex page moves to the physical LEFT
        ck(dbl.rightIndexX < dbl.leftIndexX, "double LTR: x-order must FLIP (rightIndex page physically LEFT) (rx=" + dbl.rightIndexX + " lx=" + dbl.leftIndexX + ")")

        // --- gutter shadow present for a pair ---
        ck(dbl.gutterVisible === true, "double: a pair with gutterStrength>0 must show the gutter shadow")
        dbl.gutterStrength = 0
        ck(dbl.gutterVisible === false, "double: gutterStrength 0 must hide the gutter shadow")
        dbl.gutterStrength = 0.35

        // --- DECODE REFRESH (regression guard for C1): the double surface had NO pageReady listener,
        // so a freshly-navigated unit stayed blank until you navigated away and back. pageReady must
        // bump readyRev AND re-drive the image `source` to the fresh ?rev= url. ---
        var drr0 = dbl.readyRev
        var dSrcBefore = String(dbl.rightSource)
        ck(dSrcBefore.indexOf("rev=0") >= 0, "double: the pair's rightIndex source must start at ?rev=0, got '" + dSrcBefore + "'")
        coreDouble.emitPageReady(dbl.unit.rightIndex)   // decode of the right page lands
        ck(dbl.readyRev === drr0 + 1, "double: pageReady must bump readyRev, got " + dbl.readyRev)
        ck(String(dbl.rightSource).indexOf("rev=1") >= 0, "double: after pageReady the image source must re-evaluate to ?rev=1 (unit would stay BLANK otherwise), got '" + dbl.rightSource + "'")

        // --- zoom clamps to [100,260], zoomFactor tracks it ---
        dbl.setZoom(400)
        ck(dbl.zoomPercent === 260, "double: zoom must clamp to 260 (asked 400), got " + dbl.zoomPercent)
        ck(approx(dbl.zoomFactor, 2.6), "double: zoomFactor must track clamp (2.6), got " + dbl.zoomFactor)
        dbl.setZoom(50)
        ck(dbl.zoomPercent === 100, "double: zoom must clamp to 100 (asked 50), got " + dbl.zoomPercent)
        dbl.setZoom(140)
        ck(dbl.zoomPercent === 140, "double: an in-range zoom must apply (140), got " + dbl.zoomPercent)

        // --- pan clamps to bounds (at zoom 260, panXMax = width*(2.6-1) = 800*1.6 = 1280) ---
        dbl.setZoom(260)
        ck(approx(dbl.panXMax, 1280), "double: panXMax at zoom 260 must be 1280, got " + dbl.panXMax)
        dbl.panBy(99999, 0)
        ck(approx(dbl.panX, dbl.panXMax), "double: pan right must clamp to panXMax, got " + dbl.panX)
        dbl.panBy(-99999, 0)
        ck(approx(dbl.panX, 0), "double: pan left must clamp to 0, got " + dbl.panX)
        dbl.panBy(0, 99999)
        ck(dbl.panY >= 0 && dbl.panY <= dbl.panYMax + 1e-6, "double: panY must clamp to [0,panYMax], got " + dbl.panY + " (max " + dbl.panYMax + ")")

        // --- a unit change RESETS pan only — zoom SURVIVES (a magnified volume must stay
        // magnified across turns; only the pan offset is stale on a new unit) ---
        dbl.setZoom(220)
        dbl.panBy(200, 0)
        ck(dbl.zoomPercent === 220 && dbl.panX > 0, "double: precondition — zoom/pan applied before unit change")
        coreDouble.units[7] = { rightIndex: 7, leftIndex: 8, spread: false, coverAlone: false }
        dbl.currentPage = 8      // new anchor -> new unit
        ck(dbl.zoomPercent === 220, "double: a unit change must NOT reset zoom, got " + dbl.zoomPercent)
        ck(approx(dbl.panX, 0) && approx(dbl.panY, 0), "double: a unit change must RESET pan to 0, got x=" + dbl.panX + " y=" + dbl.panY)
        dbl.setZoom(100)   // restore baseline before the next block

        // --- zoom survives a page turn — you must be able to read a whole volume magnified ---
        dbl.setZoom(160)
        ck(dbl.clampedZoom === 160, "double: setZoom(160) takes")
        dbl.panBy(50, 50)
        dbl.currentPage = dbl.currentPage + 2   // a page turn
        ck(dbl.clampedZoom === 160, "double: zoom must SURVIVE a page turn, got " + dbl.clampedZoom)
        ck(dbl.panX === 0 && dbl.panY === 0, "double: pan resets to origin on a turn (zoom does not)")
        dbl.setZoom(100)   // restore so later assertions in the file are unaffected

        // --- spread / coverAlone / single render ONE full-width image, no gutter, no crop ---
        coreDouble.units[2] = { rightIndex: 2, leftIndex: -1, spread: true, coverAlone: false }
        dbl.currentPage = 3
        ck(dbl.isPair === false, "double: a spread unit must NOT be a pair")
        ck(dbl.imageCount === 1, "double: a spread must render ONE image, got " + dbl.imageCount)
        ck(dbl.gutterVisible === false, "double: a spread must show NO gutter shadow")
        ck(approx(dbl.singleImageWidth, dbl.width), "double: a spread image must be FULL viewport width (no crop), got " + dbl.singleImageWidth + " vs " + dbl.width)

        coreDouble.units[0] = { rightIndex: 0, leftIndex: -1, spread: false, coverAlone: true }
        dbl.currentPage = 1
        ck(dbl.imageCount === 1 && dbl.gutterVisible === false, "double: a coverAlone unit must render ONE image, no gutter")

        coreDouble.units[5] = { rightIndex: 5, leftIndex: -1, spread: false, coverAlone: false }
        dbl.currentPage = 6
        ck(dbl.imageCount === 1 && dbl.gutterVisible === false, "double: a single (leftIndex<0) unit must render ONE image, no gutter")
    }

    // maxSeen pair-anchor contract: showing a unit emits unitShown(highest 1-based page in unit)
    function runDoubleMaxSeen() {
        capturedHighest = -1; unitShownCount = 0
        coreDouble.units[8] = { rightIndex: 8, leftIndex: 9, spread: false, coverAlone: false }
        var dbl = makeDouble({ "currentPage": 1, "rtl": true })
        if (!dbl) { failures.push("double-maxseen: createObject returned null"); return }
        dbl.unitShown.connect(function (hi) { capturedHighest = hi; unitShownCount += 1 })
        dbl.currentPage = 9      // anchor into the [8,9] pair unit
        // reading-HIGHEST page = max(8,9) = 9 (0-based) -> 10 (1-based, the shell's maxSeen scale)
        ck(capturedHighest === 10, "double: unitShown must fire with the unit's reading-highest page max(right,left)+1 = 10, got " + capturedHighest)
        ck(unitShownCount >= 1, "double: showing a unit must emit unitShown at least once")
    }

    // FRESH-OPEN in double mode: manga now DEFAULTS to double-page, so the double surface is active
    // from the start and the entry becomes available AFTER (async volume load). The unit MUST
    // recompute on entryChanged/pairingChanged even though `active` and `currentPage` never change —
    // else the surface holds its empty first-frame unit forever and the page stays BLACK.
    function runDoubleFreshOpen() {
        coreFresh.loaded = false            // core has no entry yet (first paint, before pages arrive)
        var dbl = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 1, "core": coreFresh
        })
        if (!dbl) { failures.push("double-fresh: createObject returned null"); return }
        ck(dbl.imageCount === 0, "double-fresh: before the entry loads the surface must have NO unit (blank), got " + dbl.imageCount)
        // the volume finishes loading: the pairing units become available + the core signals it
        coreFresh.units[0] = { rightIndex: 0, leftIndex: -1, spread: false, coverAlone: true }
        coreFresh.loadEntry()               // sets loaded + emits entryChanged + pairingChanged (NO currentPage/active change)
        ck(dbl.imageCount === 1, "double-fresh: after entryChanged/pairingChanged the unit MUST recompute (cover renders), got " + dbl.imageCount)
        ck(dbl.unit.rightIndex === 0, "double-fresh: the recomputed unit must be the loaded cover (rightIndex 0), got " + dbl.unit.rightIndex)
        ck(coreFresh.setVisibleCalls >= 1, "double-fresh: the load must drive core.setVisible so the cover decodes (else no request), got " + coreFresh.setVisibleCalls)
    }

    Timer { id: phaseTimer; interval: 30; running: false; onTriggered: harness.runPhaseTwo() }

    function runPhaseTwo() {
        try {
            runStripFailure()
            runDouble()
            runDoubleMaxSeen()
            runDoubleFreshOpen()
        } catch (e) {
            failures.push("exception during phase two: " + e.message)
        }
        report()
    }

    function runChecks() {
        try {
            runStrip()
        } catch (e) {
            failures.push("exception during strip checks: " + e.message)
        }
        // let one frame settle (ListView delegate creation) before the double/failure phase
        phaseTimer.start()
    }

    Component.onCompleted: {
        try {
            stripComp = Qt.createComponent("../qml/comicreader/ComicReaderStripSurface.qml")
            if (stripComp.status === Component.Error) throw new Error("strip component: " + stripComp.errorString())
            doubleComp = Qt.createComponent("../qml/comicreader/ComicReaderDoubleSurface.qml")
            if (doubleComp.status === Component.Error) throw new Error("double component: " + doubleComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_SURFACES_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    // safety net — a true hang (not a thrown error) still fails loudly instead of stalling CI
    Timer {
        interval: 8000; running: true
        onTriggered: { console.log("COMICREADER_SURFACES_FAIL: timeout"); Qt.exit(1) }
    }
}

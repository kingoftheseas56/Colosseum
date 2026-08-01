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
//       lands ~168px/notch into a bounded backlog, and each 16ms drain takes ~0.38 of the backlog
//       clamped to a max step (no giant jump), draining smoothly in float sub-pixel space.
//     * a per-page pageFailed(page,code) shows the typed placard for THAT page's delegate only.
//     * RESTORE is a one-shot COMMAND from the shell, never a bound fraction: seekToPage(n) lands on
//       the BACKEND's page top (clamped to the span) and haltScrollAt(y) pins contentY exactly; both
//       drop any in-flight glide and emit NO scrolled()/pageInView() — the surface owns no resume
//       state, so nothing can loop (surface scroll -> shell fraction -> surface applies -> scroll).
//
//   DOUBLE (ComicReaderDoubleSurface):
//     * (Task 4) the unit PAINTS AS ONE THING: with one half undecoded the pair paints NOTHING and
//       shows a restrained placeholder; when both halves have pixels it appears in one step; a
//       terminal failure in either half paints the good side beside a typed placard on the broken one
//       and stops waiting; a healed page returns it to normal; and a core with no presentationForPage
//       degrades to painting (the pre-Task-4 behaviour), never to a blank screen.
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
//     * ONE shared scale for the whole displayed unit: a MISMATCHED pair (1550x2200 next to
//       1500x2200 — routine in scanned volumes) is drawn at a single scale, so both halves keep
//       their true relative size and land at the same height; the two inner edges meet flush on the
//       centre line in BOTH directions; a unit shorter than the viewport centres as a block with
//       each half centred in its band; and the gutter shadow is the DRAWN PAIR's height/offset, not
//       the window's. The natural size behind that scale is the backend's header geometry
//       (pageInfo), with the Image's own decoded size as the fallback — both paths covered below.
//
//   SINGLE (ComicReaderSingleSurface, Task 4):
//     * fit is CONTAIN, not fit-width: the WHOLE page is visible and centred at 100% zoom, with no
//       pan headroom (the one place Single deliberately differs from Pair).
//     * two stacked tiers — the preview tier under the hq tier, two distinct requests.
//     * zoom PRESERVES THE CENTROID: whatever sat under the viewport centre is still there after a
//       zoom step, from a centred AND from a panned-to-the-bottom start. A clamp-only zoom (what Pair
//       does) fails the second case, which is what makes that assertion worth having.
//     * zoom clamps [100,260]; pan clamps to the page's own bounds; a page turn resets pan and KEEPS
//       zoom; the turn pins exactly that page for decode.
//     * a failed page shows the typed placard and heals.
//     * presented(page, 0) fires ONCE per page even though two tiers reach Ready.
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
    // Find a descendant by objectName. Task 11 needs it to reach the error card's two actions,
    // which live inside a shared leaf component rather than at a surface's top level.
    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var f = byName(kids[i], name)
            if (f) return f
        }
        return null
    }
    // number formatter that survives an ABSENT property — a failure message must never throw, or the
    // first missing property masks every other assertion in the run.
    function fx(v, n) { return (typeof v === "number" && isFinite(v)) ? v.toFixed(n) : String(v) }

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
        // TRUE source geometry per page, exactly as the real core's pageInfo() reports it
        // (sourceWidth/sourceHeight — learned from the file HEADER ahead of the decode,
        // ComicReaderDecode::onWorkerDimensions -> ComicReaderCore::onMetaReady). EMPTY by default,
        // so every pre-existing scenario keeps taking the "the backend knows no size yet" path.
        property var pageSizes: ({})
        // per-page imageUrl override. Used by the decoded-pair scenario to serve REAL data: URL
        // fixtures whose pixel dimensions differ, so the Image's implicitWidth/implicitHeight are
        // genuinely mismatched rather than injected.
        property var pageUrls: ({})
        // whether an entry is loaded — before openEntry, unitForPage returns a degenerate empty unit
        // (mirrors the real core with no entry). loadEntry() flips it + fires the load signals.
        property bool loaded: true
        // signals (shape parity with the real core)
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        // Task 11: retryPage() cleared this page's verdict. The real core emits it because clearing
        // the verdict changes pageInfo()'s answer and nothing else QML can see moves with it.
        signal pageRetried(int page)
        signal stripCompensation(real delta)
        signal entryChanged()      // real core emits this on openEntry (ComicReaderCore.cpp:201)
        signal pairingChanged()    // ...and this (ComicReaderCore.cpp:202), + on every rebuildUnits
        function loadEntry() { loaded = true; entryChanged(); pairingChanged() }
        // Task 4: the real core's imageUrl carries a TIER (Task 2's preview/hq/thumbnail split) on a
        // default argument. Mirrored here so the Single surface's two stacked tiers can be told apart
        // in a url; absent/empty normalises to hq exactly as the C++ side does.
        function imageUrl(page, tier) {
            if (pageUrls[page] !== undefined) return pageUrls[page]
            var r = (pageRevs[page] !== undefined) ? pageRevs[page] : 0
            var t = (tier === undefined || tier === "") ? "hq" : String(tier)
            return "image://comicreader/1/" + page + "?rev=" + r + "&tier=" + t
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
        // Task 4: per-page presentation truth, mirroring the real core's m_readyPages / PageError.
        // A page not listed in `pageStates` is READY, so every pre-Task-4 scenario in this file keeps
        // painting exactly as it did; the gating scenarios opt in by naming pages explicitly.
        property var pageStates: ({})    // 0-based page -> "ready" | "waiting" | "error"
        property var pageErrors: ({})    // 0-based page -> PageError wire code
        function _pageState(p) {
            return (pageStates[p] !== undefined) ? String(pageStates[p]) : "ready"
        }
        function _pageErrorOf(p) {
            return (pageErrors[p] !== undefined) ? String(pageErrors[p]) : "none"
        }
        function pageInfo(page) {
            var s = pageSizes[page]
            var err = _pageErrorOf(page)
            if (s !== undefined)
                return { error: err, sourceWidth: s.w, sourceHeight: s.h }
            return { error: err }       // the real core also omits geometry it has not learned yet
        }
        // ComicReaderCore::presentationForPage — the unit as ONE thing. Same order of verdicts as the
        // C++ (error before waiting), same empty answer with no entry, and it reads unitForPage rather
        // than re-deriving membership, because that is the property the real one is required to have.
        function presentationForPage(page) {
            if (!loaded) return ({})
            var u = unitForPage(page)
            var out = { rightIndex: u.rightIndex, leftIndex: u.leftIndex,
                        spread: !!u.spread, coverAlone: !!u.coverAlone,
                        state: "ready", errorCode: "none" }
            var members = []
            if (u.rightIndex >= 0) members.push(u.rightIndex)
            if (u.leftIndex >= 0) members.push(u.leftIndex)
            if (members.length === 0) { out.state = "waiting"; return out }
            for (var i = 0; i < members.length; i++) {
                if (_pageState(members[i]) === "error") {
                    out.state = "error"
                    out.errorCode = _pageErrorOf(members[i])
                    return out
                }
            }
            for (var j = 0; j < members.length; j++) {
                if (_pageState(members[j]) !== "ready") { out.state = "waiting"; return out }
            }
            return out
        }
        function setVisible(pages) { setVisibleCalls += 1; lastVisible = pages }
        function setStripViewport(top, height) {
            setStripViewportCalls += 1; lastViewportTop = top; lastViewportHeight = height
        }
        // the backend's own page top (Q_INVOKABLE double stripPageTop(int) on the real core). The
        // ListView cannot supply this — it only realizes delegates near the viewport, so the page you
        // are seeking TO has no y to read yet, which is exactly why an immediate seek lands at 0.
        // Matches fillStripModel's `top: i * 1220`; out of range is 0, never a crash (the real core's
        // contract, pinned in comicreader_core_harness T16).
        property int stripPageTopCalls: 0
        property real stripPageTopForce: -1      // >=0 overrides the answer (to exercise the clamp)
        function stripPageTop(page) {
            stripPageTopCalls += 1
            if (stripPageTopForce >= 0) return stripPageTopForce
            if (!stripModel || page < 0 || page >= stripModel.count) return 0
            return page * 1220
        }
        // The other two halves of the backend's strip geometry (ComicReaderCore::stripPageAtCenter /
        // stripPageHeight). The surface asks THESE rather than the drawn column for presented(),
        // because after a restore the ListView has no item at the new position — so a fake that
        // lacked them would make that path untestable, which is exactly how the gap survived.
        // Both read the MODEL, which is what the real backend's strip geometry is.
        function stripPageHeight(page) {
            if (!stripModel || page < 0 || page >= stripModel.count) return 0
            return stripModel.get(page).displayHeight
        }
        function stripPageAtCenter(top, viewportHeight) {
            if (!stripModel || stripModel.count <= 0) return -1
            var centre = top + viewportHeight / 2
            for (var i = 0; i < stripModel.count; i++) {
                var r = stripModel.get(i)
                if (centre >= r.top && centre < r.top + r.displayHeight) return i
            }
            return -1
        }
        function setStripViewportWidth(w) { setStripViewportWidthCalls += 1; lastViewportWidth = w }
        // Task 8: the RETENTION window. Task 2 built it and left it with no production caller; the
        // strip surface is that caller now. The spy records every call because the whole question
        // this seam raises is HOW OFTEN — requestRange's own doc comment warns that a call whose
        // range differs walks both cache hashes under both mutexes and frees QImages on the GUI
        // thread, contending the mutex every provider worker takes for every page fetch.
        property int requestRangeCalls: 0
        property var requestRanges: []
        function requestRange(first, last) {
            requestRangeCalls += 1
            requestRanges.push({ first: first, last: last })
        }
        function lastRange() {
            return requestRanges.length ? requestRanges[requestRanges.length - 1] : null
        }
        function resetRangeSpy() { requestRangeCalls = 0; requestRanges = [] }
        // Task 8: the never-resize spy. Auto-scroll must have NO path to the strip geometry, so the
        // one call that could resize the page is counted and must stay at zero across every motion.
        property int setStripLayoutCalls: 0
        property var lastStripLayout: null
        function setStripLayout(widthPct, gap, top, height) {
            setStripLayoutCalls += 1
            lastStripLayout = { widthPct: widthPct, gap: gap }
            return (top === undefined) ? 0 : top
        }
    }

    // a strip model (Task-6 roles: pageIndex/top/displayWidth/displayHeight/ready/errorCode)
    ListModel { id: stripModelA }
    ListModel { id: stripModelFail }
    ListModel { id: stripModelAuto }    // Task 8: Auto-scroll, on a column of its own
    ListModel { id: stripModelRange }   // Task 8: the requestRange retention window
    ListModel { id: stripModelLoupe }   // Task 9: the drawn-row report the Loupe reads

    FakeCore { id: coreStrip }
    FakeCore { id: coreAuto }      // Task 8: Auto-scroll
    FakeCore { id: coreRange }     // Task 8: requestRange
    FakeCore { id: coreFail }
    FakeCore { id: coreDouble }
    FakeCore { id: coreFresh }
    FakeCore { id: coreScale }     // MISMATCHED pair, sizes from the backend (pageInfo)
    FakeCore { id: coreShort }     // pair SHORTER than the viewport (centring + gutter)
    FakeCore { id: coreZoomPan }   // TALL pair: real headroom on BOTH pan axes at every zoom
    FakeCore { id: coreDecoded }   // no backend sizes: the file fixtures' pixels drive implicitWidth
    FakeCore { id: coreGate }      // Task 4: the pair's presentation gate (waiting / ready / error)
    // A core that genuinely does NOT have presentationForPage. It has to be a SEPARATE component, not
    // a flag on FakeCore: a QML function property is always truthy, so a FakeCore that merely returned
    // undefined would still pass the surface's `core.presentationForPage` guard and exercise the
    // empty-answer path instead of the absent-seam path.
    component FakeCoreNoPresentation: QtObject {
        property var pageSizes: ({})
        property var pageUrls: ({})
        property var units: ({})
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal entryChanged()
        signal pairingChanged()
        function imageUrl(page, tier) {
            if (pageUrls[page] !== undefined) return pageUrls[page]
            return "image://comicreader/1/" + page + "?rev=0&tier=" + ((tier === undefined) ? "hq" : tier)
        }
        function unitForPage(page) {
            if (units[page] !== undefined) return units[page]
            return { rightIndex: page, leftIndex: -1, spread: false, coverAlone: false }
        }
        function pageInfo(page) {
            var s = pageSizes[page]
            if (s !== undefined) return { error: "none", sourceWidth: s.w, sourceHeight: s.h }
            return { error: "none" }
        }
        function setVisible(pages) {}
    }
    FakeCoreNoPresentation { id: coreStub }
    FakeCore { id: coreSingle }    // Task 4: Single Page geometry, tiers, zoom centroid, pan clamp
    FakeCore { id: coreSinglePix } // Task 4: Single Page presented(), driven by real file fixtures
    FakeCore { id: coreSingleDefer } // Task 4: the DEFERRED re-check, asserted in phase three
    FakeCore { id: coreLoupeStrip }  // Task 9: the drawn-row report, on a column of its own
    FakeCore { id: coreLoupeSingle } // Task 9: the drawn-page report for Single Page

    property var stripComp: null
    property var doubleComp: null
    property var singleComp: null
    property var stripSurface: null
    property var stripFailSurface: null
    property var doubleSurface: null

    // ---- unitShown capture ----
    property int capturedHighest: -1
    property int unitShownCount: 0

    // ---- presented() capture (Task 4). All three surfaces emit it; nothing consumes it until
    // Task 11, so these counters are what proves it fires when — and only when — pixels are up. ----
    property int stripPresentedCount: 0
    property int stripPresentedPage: -1
    property real stripPresentedFrac: -1
    property int singlePresentedCount: 0
    property int singlePresentedPage: -1
    property real singlePresentedFrac: -1
    property int doublePresentedCount: 0
    property int doublePresentedPage: -1
    property real doublePresentedFrac: -1
    property int deferredPresentedCount: 0
    property int deferredPresentedPage: -1

    // ---- strip user-signal capture: a PROGRAMMATIC restore must emit none of these ----
    property int stripScrolledCount: 0
    // manualNavigation() is WHEEL provenance specifically ("a real gesture happened"), unlike the
    // provenance-blind tracking signals above. E2 routes keyboard scrolling through the same drain
    // as the wheel, so this counter is what proves the shared path did not forge a wheel gesture.
    property int stripManualNavCount: 0

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
        stripSurface.scrolled.connect(function (f) { harness.stripScrolledCount += 1 })
        stripSurface.pageInView.connect(function (p) { harness.stripScrolledCount += 1 })
        stripSurface.manualNavigation.connect(function () { harness.stripManualNavCount += 1 })
        stripSurface.presented.connect(function (p, f) {
            harness.stripPresentedCount += 1
            harness.stripPresentedPage = p
            harness.stripPresentedFrac = f
        })
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

        // --- TWO TIERS, stacked (Task 8). The column meets new pages continuously, so a single-tier
        // strip waited for the full-resolution page once per page SCROLLED PAST, not once per page
        // turn: black band, then the whole page at once. Same stack, same 90ms cross-fade and same
        // tier vocabulary as Single and Pair, so a page sharpening reads the same wherever you meet
        // it. The scaled tier was sized for exactly this pair (kScaledEntriesPerPage == 2). ---
        var dTier = stripSurface.itemAt(0)
        ck(dTier !== null, "strip tiers: page 0's delegate must exist")
        if (dTier) {
            ck(String(dTier.previewSource).indexOf("tier=preview") >= 0,
               "strip tiers: the delegate must request the PREVIEW tier, got '" + dTier.previewSource + "'")
            ck(String(dTier.imageSource).indexOf("tier=hq") >= 0,
               "strip tiers: ...and the HQ tier, got '" + dTier.imageSource + "'")
            ck(dTier.previewCap > 0 && dTier.previewCap < dTier.hqCap,
               "strip tiers: the preview must be capped SMALLER than hq (first pixels, soonest), got "
               + dTier.previewCap + " vs " + dTier.hqCap)
            // the fade is a RULE, never the animated number: an offscreen harness never ticks a
            // Behavior, so a synchronous read of `opacity` would prove nothing either way.
            ck(dTier.hqShown === false,
               "strip tiers: with hq not loaded the preview is what shows, got hqShown=" + dTier.hqShown)
        }

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


        // --- B3: TRACKING IS PROVENANCE-BLIND (bug 1) + THROTTLED, not per-frame (bug 2). This block
        // runs BEFORE the first _intakeWheel call below, so _userInteracted is still false here — that
        // is the whole point: the OLD contract gated the emit on _userInteracted, which only a wheel
        // gesture ever set (_intakeWheel), so reading via Space/PageUp/PageDown/scrub-drag/Home/End
        // reported NOTHING to the HUD counter, the gold thread, or the Continue record until one
        // incidental wheel notch anywhere in the session silently "fixed" it. The NEW contract gates
        // on _programmatic ONLY: any other move is a move, and gets throttled to <= one emit per
        // ~80ms window (Reader 1's pageTrack) rather than once per contentY tick. ---
        ck(stripSurface._userInteracted === false,
           "B3 precondition: no wheel gesture has fired yet, _userInteracted must still be false")

        var b3Before = harness.stripScrolledCount
        // three rapid PLAIN writes in one window — simulates keyboard Space/PageUp/PageDown or a
        // scrub-bar drag: NOT the wheel path (no _intakeWheel call), and NOT _programmatic.
        stripSurface.contentY = 300
        stripSurface.contentY = 600
        stripSurface.contentY = 900
        // _emitPending is the scheduling seam (mirrors _reportPending / _flushViewportReport below) —
        // reading it is the ONLY way to prove the plain writes above actually SCHEDULED an emit rather
        // than being silently dropped by a _userInteracted gate: _emitUserScroll() itself has no
        // gating logic, so calling it directly would "pass" even against the old buggy code and prove
        // nothing about bug 1.
        ck(stripSurface._emitPending === true,
           "B3 (bug 1 fix): a plain non-programmatic contentY move must SCHEDULE an emit even though no "
           + "wheel gesture has ever fired (old code gated scheduling on _userInteracted, set ONLY by "
           + "_intakeWheel, so Space/PageUp/PageDown/scrub/Home/End emitted NOTHING), got _emitPending="
           + stripSurface._emitPending)
        ck(harness.stripScrolledCount === b3Before,
           "B3 (bug 2 fix): the emit must be THROTTLED, not per-tick — a burst of plain contentY writes must produce ZERO immediate emissions, got "
           + (harness.stripScrolledCount - b3Before))
        var presentedBefore = harness.stripPresentedCount
        stripSurface._flushEmit()      // the same entry point the 80ms throttle timer calls
        ck(harness.stripScrolledCount === b3Before + 2,
           "B3: once the throttle window elapses the scheduled emit must fire scrolled()+pageInView(), got "
           + (harness.stripScrolledCount - b3Before))

        // --- PRESENTED (Task 4): the strip reports the page whose pixels are on screen AND how far
        // down it the viewport centre sits. It rides the SAME throttled flush as pageInView (one
        // emit per window, never per contentY tick), and the fraction is real geometry, not a
        // pages*ratio estimate: contentY is 900 and the viewport is 480 tall, so the centre is at
        // 1140 inside page 0 — whose delegate this scenario resized to 1700 — i.e. 0.671 down it.
        // This is the one surface where the fraction is ever non-zero, which is why it exists. ---
        ck(harness.stripPresentedCount === presentedBefore + 1,
           "B3/presented: the throttled flush must emit presented() exactly ONCE, got "
           + (harness.stripPresentedCount - presentedBefore))
        ck(harness.stripPresentedPage === 1,
           "B3/presented: the presented page must be the 1-based page under the viewport centre (1), got "
           + harness.stripPresentedPage)
        ck(approx(harness.stripPresentedFrac, 1140.0 / 1700.0, 0.01),
           "B3/presented: withinPageFraction must be the geometric position inside that page ("
           + fx(1140.0 / 1700.0, 3) + "), got " + fx(harness.stripPresentedFrac, 3))
        ck(stripSurface._emitPending === false,
           "B3: flushing the scheduled emit must clear _emitPending, got " + stripSurface._emitPending)
        ck(stripSurface._userInteracted === false,
           "B3: a plain contentY move must NOT retroactively set _userInteracted — that flag stays wheel-only")

        // a _programmatic move (haltScrollAt, same door as seekToPage/compensation/applyLayout) must
        // STILL emit nothing — that guard is load-bearing: it is what stops a restore from clobbering
        // the shell's page.
        var b3Before2 = harness.stripScrolledCount
        stripSurface.haltScrollAt(50)
        ck(stripSurface._emitPending === false,
           "B3: a _programmatic move (haltScrollAt) must NOT schedule an emit at all, got _emitPending=" + stripSurface._emitPending)
        ck(harness.stripScrolledCount === b3Before2,
           "B3: a _programmatic move (haltScrollAt) must still emit NOTHING, got "
           + (harness.stripScrolledCount - b3Before2))

        // --- smooth-wheel float accumulator ---
        // reset to a clean scroll state
        stripSurface.contentY = 0
        stripSurface._pendingWheelPx = 0
        stripSurface._smoothY = 0
        // 3 notches DOWN: angleDelta -360 -> intake ~ -360*1.4 = -504 -> +504 into the backlog
        // (168px/notch — the reader-1 house tuning restored on Hemanth's 2026-07-25 ruling)
        stripSurface._intakeWheel(-360, 0)
        var pend0 = stripSurface._pendingWheelPx
        ck(approx(pend0, 504, 1.0), "wheel: 3-notch intake (~168px/notch) must land ~504px in the backlog, got " + pend0)
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
        // put in (504px here), which is a whole number by construction and would pass even if every
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
        ck(stripSurface._smoothY === 1234.5, "wheel: haltScrollAt must re-anchor the float accumulator, got " + stripSurface._smoothY)

        // --- E2: keyboard / API repositions ride the SAME drain as the wheel ---
        // A raw contentY write bypasses both the glide and the backlog: press Space mid-glide and the
        // view jumps AND THEN keeps sliding on the leftover wheel input. Reader 1 routes keys through
        // smoothScrollBy and pins instant moves through haltScrollAt. Space should feel like one big
        // wheel notch, not a teleport.
        stripSurface.haltScrollAt(0)
        stripSurface.smoothScrollBy(300)
        ck(stripSurface._pendingWheelPx === 300,
           "glide: smoothScrollBy must feed the SAME drain backlog as the wheel, got " + stripSurface._pendingWheelPx)
        ck(stripSurface._drainFresh === true,
           "glide: a glide from idle must mark the drain FRESH, exactly like a wheel intake from idle")

        // An instant reposition mid-glide drops the leftover backlog — no jump-then-slide.
        stripSurface.haltScrollAt(500)
        ck(stripSurface._pendingWheelPx === 0 && stripSurface.contentY === 500,
           "glide: haltScrollAt must pin AND drop in-flight backlog, got contentY=" + stripSurface.contentY
           + " backlog=" + stripSurface._pendingWheelPx)

        // PROVENANCE: manualNavigation() means "a real WHEEL gesture happened". A keyboard/API glide
        // must not forge one — otherwise every Space press would look like a mouse gesture to any
        // future consumer of that signal. (Tracking itself is provenance-blind; this is only the
        // wheel-specific signal.)
        var manualBefore = harness.stripManualNavCount
        stripSurface.smoothScrollBy(120)
        ck(harness.stripManualNavCount === manualBefore,
           "glide: smoothScrollBy must NOT emit manualNavigation() — that signal is wheel provenance")
        stripSurface.haltScrollAt(0)

        // ...while the wheel path still does emit it, so the refactor did not hollow the signal out.
        stripSurface._intakeWheel(-120, 0)
        ck(harness.stripManualNavCount === manualBefore + 1,
           "glide: the WHEEL path must still emit manualNavigation() after the refactor, got "
           + harness.stripManualNavCount + " vs " + (manualBefore + 1))
        stripSurface.haltScrollAt(0)

        // --- F5 seam: atEnd, the shell's "is there anything left to scroll" question ---
        // The span is re-read immediately before every step, never cached: contentHeight moves as
        // the ListView relayouts, and haltScrollAt does NOT clamp — caching it once parked the
        // column 97,000px past the end of the book and made the first draft of this test fail for
        // its own reasons rather than the code's.
        function spanNowF5() {
            stripSurface.forceRelayout()
            return stripSurface.contentHeight - stripSurface.height
        }

        // UNSETTLED GEOMETRY FIRST (the defect Codex's cross-review caught): until the backend has
        // learned the LAST page's real size, contentHeight is an underestimate and the span is
        // short. Scrolling to that short span must NOT be reported as the end, or the reader
        // announces the end of a volume you are in the middle of.
        stripSurface.haltScrollAt(0)
        ck(coreStrip.pageSizes[199] === undefined,
           "atEnd: precondition - the last page must start UNMEASURED for this check to mean anything")
        stripSurface.haltScrollAt(stripSurface.contentHeight - stripSurface.height)
        ck(stripSurface.atEnd === false,
           "atEnd: parked at the END of an UNMEASURED column must report false (the height is an "
           + "estimate; announcing here is the false end-of-volume toast)")

        // now the backend learns the last page's true size and the same position IS the end
        coreStrip.pageSizes[199] = { w: 800, h: 1200 }
        coreStrip.emitPageReady(199)   // the decode lands -> readyRev bumps -> atEnd re-evaluates
        ck(stripSurface.atEnd === true,
           "atEnd: once the last page is measured, the same parked position must report the end")

        stripSurface.haltScrollAt(0)
        ck(spanNowF5() > 0, "atEnd: fixture must have a scrollable span to test against, got " + spanNowF5())
        ck(stripSurface.atEnd === false, "atEnd: must be false at the top of a long book")

        stripSurface.haltScrollAt(spanNowF5())
        ck(stripSurface.atEnd === true, "atEnd: must be true once parked at the bottom (contentY="
           + stripSurface.contentY + " span=" + spanNowF5() + ")")

        // It counts the IN-FLIGHT backlog: a second key pressed while the first is still gliding
        // toward the bottom must not read the not-yet-arrived position and announce the end early.
        stripSurface.haltScrollAt(spanNowF5() - 2000)
        ck(stripSurface.atEnd === false, "atEnd: must be false 2000px short of the bottom (contentY="
           + stripSurface.contentY + " span=" + spanNowF5() + ")")
        stripSurface.smoothScrollBy(5000)                 // a glide that will land past the bottom
        ck(stripSurface.atEnd === true,
           "atEnd: a glide already heading past the bottom must count as at-the-end (backlog aware)")
        stripSurface.haltScrollAt(0)

        // --- E6: compensation is CLAMPED (placed here, at the end of the strip checks, because it
        // moves contentY freely and the B3 emit-throttle checks above depend on their own state) ---
        // Near the top of a book a page above the fold can shrink by more than the current scroll
        // position; unclamped, contentY went negative and left a black band above page 1 until
        // something else moved the view.
        stripSurface.haltScrollAt(40)
        coreStrip.stripCompensation(-200)
        ck(approx(stripSurface.contentY, 0),
           "strip: a compensation larger than the scroll position must clamp to 0, not go negative, got "
           + stripSurface.contentY)
        ck(approx(stripSurface._smoothY, 0),
           "strip: the float accumulator must follow the clamped position, got " + stripSurface._smoothY)

        // ...and it clamps at the bottom too, so a growing page cannot push past the end. The span
        // is read fresh from the surface at assert time rather than cached in a local: contentHeight
        // moves with relayout, and a cached copy is what made the first draft of this check compare
        // against a stale number.
        stripSurface.forceRelayout()
        stripSurface.haltScrollAt(stripSurface.contentHeight - stripSurface.height - 30)
        coreStrip.stripCompensation(9999)
        ck(approx(stripSurface.contentY, stripSurface.contentHeight - stripSurface.height),
           "strip: compensation must clamp to the scrollable span at the bottom, got "
           + stripSurface.contentY + " vs span "
           + (stripSurface.contentHeight - stripSurface.height))
        stripSurface.haltScrollAt(0)


        // --- RESTORE (B2): the surface is a PAINTER. It restores nothing itself — the shell puts the
        // column somewhere by CALLING seekToPage()/haltScrollAt(). This replaces the old bound
        // `resumeFraction` + `_resumeApplied` latch, which was a feedback loop (the surface's own
        // onScrolled writes the shell's fraction, which re-drove the binding) broken by a latch that
        // was per-OBJECT-LIFETIME — the reader is a persistent child, so the second book never resumed.
        ck(stripSurface.resumeFraction === undefined,
           "restore: the surface must expose NO bound resumeFraction (a scroll->fraction->apply loop)")

        // seekToPage lands on the BACKEND's page top, not on a delegate's y (a far page has no
        // delegate — that is exactly why an immediate seek used to land at the top of the book).
        stripSurface._intakeWheel(-360, 0)            // a glide still in flight must not fight the seek
        var seekOk = stripSurface.seekToPage(10)
        ck(seekOk === true, "restore: seekToPage must report success once the column is laid out")
        ck(approx(stripSurface.contentY, 12200, 0.001),
           "restore: seekToPage(10) must land on the backend's page top 12200, got " + stripSurface.contentY)
        ck(stripSurface._pendingWheelPx === 0, "restore: a seek must drop any in-flight scroll backlog, got " + stripSurface._pendingWheelPx)

        // ...and it CLAMPS to the scrollable span rather than assigning past the end of the book.
        // The column's heights are ESTIMATES until decodes land, so a backend top can legitimately
        // exceed the span the ListView currently reports — forced here rather than hoped for.
        var spanNow = stripSurface.contentHeight - 480
        coreStrip.stripPageTopForce = spanNow + 50000
        stripSurface.seekToPage(199)
        ck(approx(stripSurface.contentY, spanNow, 0.001),
           "restore: seekToPage must clamp to the scrollable span " + spanNow + ", got " + stripSurface.contentY)
        coreStrip.stripPageTopForce = -1

        // ---- Task 11: land INSIDE a page, not merely on it ----
        // "Returning to a book should land on the same panel area instead of only the approximate
        // page." seekToPageFraction is the exact inverse of what _emitPresented reports — it
        // measures (viewportCentre - pageTop) / pageHeight, so putting the reader back means putting
        // that same point back under the viewport centre. Page 10's top is 12200 and every fixture
        // page is 1200 tall, so 0.25 down page 10 is y = 12200 + 300 - 480/2 = 12260.
        //
        // The numbers are chosen so a wrong-but-plausible implementation cannot pass by luck: a
        // top-anchored seek gives 12200, forgetting the half-viewport gives 12500, and using the
        // page's own height as the whole column gives something far away. All three are distinct.
        var fracOk = stripSurface.seekToPageFraction(10, 0.25)
        ck(fracOk === true, "restore: seekToPageFraction must report success once the column is laid out")
        ck(approx(stripSurface.contentY, 12260, 0.001),
           "restore: seekToPageFraction(10, 0.25) must put that point under the viewport CENTRE "
           + "(12200 + 0.25*1200 - 480/2 = 12260), got " + stripSurface.contentY)
        // ...and it is NOT the same instruction as seekToPage. A fraction of 0 means "the centre sat
        // on the page's top edge", which is half a viewport HIGHER than landing the page's top at the
        // top of the screen. Keeping them distinguishable is what lets a record written before
        // pageFraction existed resume the way it always did.
        stripSurface.seekToPageFraction(10, 0)
        ck(approx(stripSurface.contentY, 12200 - 240, 0.001),
           "restore: a ZERO fraction is a real instruction (centre on the page top), not 'no opinion' — "
           + "expected 11960, got " + stripSurface.contentY)
        stripSurface.seekToPage(10)
        ck(approx(stripSurface.contentY, 12200, 0.001),
           "restore: ...and seekToPage still lands the page TOP at the viewport top, got " + stripSurface.contentY)
        // Clamped like its sibling: a fraction near the end of the last page must not assign past
        // the scrollable span.
        stripSurface.seekToPageFraction(199, 1)
        ck(stripSurface.contentY <= stripSurface.contentHeight - 480 + 0.001
               && stripSurface.contentY >= 0,
           "restore: seekToPageFraction must clamp to the scrollable span, got " + stripSurface.contentY)

        // a restore must NEVER masquerade as a user scroll — that is the loop, from the other side.
        var scrollsBefore = harness.stripScrolledCount
        var presentedBeforeRestore = harness.stripPresentedCount
        stripSurface.seekToPage(20)
        stripSurface.haltScrollAt(700)
        ck(harness.stripScrolledCount === scrollsBefore,
           "restore: a programmatic restore must emit NO scrolled()/pageInView() (that is the feedback loop), got "
           + (harness.stripScrolledCount - scrollsBefore) + " emissions")
        // presented() is the OPPOSITE case, and an earlier version of this gate had it backwards.
        // scrolled()/pageInView() write back into the shell's page and fraction, so a restore that
        // emitted them would clobber the very spot it was restoring TO. presented() writes back into
        // nothing — it is an outbound notice that the reader can now see this position — and after a
        // resume the reader genuinely IS looking at that page. Suppressing it meant the landing page of
        // every resumed book went unreported, which Task 11 (which banks progress on this signal) would
        // have inherited as a silence it could not tell apart from "never seen".
        //
        // It is DEFERRED (Qt.callLater in haltScrollAt) so it lands after the caller's own bindings
        // settle. So: nothing synchronous here, and then the same entry point callLater will reach,
        // invoked directly (the house pattern this file uses for _flushEmit / _flushViewportReport,
        // so no phase waits on a wall clock).
        ck(harness.stripPresentedCount === presentedBeforeRestore,
           "restore: presented() must be DEFERRED past the restore, not fired against the position the "
           + "reader was on before it, got " + (harness.stripPresentedCount - presentedBeforeRestore))
        stripSurface._emitPresented()
        ck(harness.stripPresentedCount === presentedBeforeRestore + 1,
           "restore: the deferred report must then fire exactly ONCE for the landing page, got "
           + (harness.stripPresentedCount - presentedBeforeRestore))
        // AND IT MUST NAME THE LANDING PAGE rather than fall silent. This is the assertion that
        // catches the real trap: the column has just been jumped from 24400 to 700, so the ListView
        // holds no item anywhere near the new centre — measured, indexAt() and itemAtIndex() both
        // answer nothing there, and neither forceLayout() nor a later event-loop pass brings them
        // back — so a delegate-derived report emits NOTHING AT ALL. The surface has to ask the
        // BACKEND (stripPageAtCenter / stripPageTop / stripPageHeight), which knows the column's
        // geometry whether or not it has been drawn. contentY 700 + half of a 480 viewport = 940,
        // inside page 0 (top 0, height 1700 after this scenario resized it) -> page 1, frac 940/1700.
        ck(harness.stripPresentedPage === 1,
           "restore: the report must NAME the landing page (1), got " + harness.stripPresentedPage)
        ck(approx(harness.stripPresentedFrac, 940.0 / 1700.0, 0.01),
           "restore: ...and carry the position INSIDE that page (" + fx(940.0 / 1700.0, 3)
           + "), so a resume cannot bank a worse fraction than the one it restored from, got "
           + fx(harness.stripPresentedFrac, 3))
        ck(harness.stripScrolledCount === scrollsBefore,
           "restore: and it must STILL not have emitted scrolled()/pageInView() — those are the "
           + "feedback loop, presented() is not")

        // --- I2: a HIDDEN (inactive) strip must NOT report its viewport — both surfaces share one
        // core/pool, so a hidden strip driving decode requests would compete with the active surface. ---
        stripSurface.active = false
        coreStrip.setStripViewportCalls = 0
        stripSurface.contentY = 3000
        stripSurface._flushViewportReport()
        ck(coreStrip.setStripViewportCalls === 0, "strip: an INACTIVE strip must NOT report its viewport, got " + coreStrip.setStripViewportCalls + " calls")

        // ...and it must not report a PRESENTATION either. An unmounted strip still holds a laid-out
        // column and still takes seekToPage/haltScrollAt from the shell across a layout switch; a
        // report from there tells Task 11 the reader saw a page that is not on screen.
        var presentedInactive = harness.stripPresentedCount
        stripSurface.haltScrollAt(2400)
        stripSurface._emitPresented()
        ck(harness.stripPresentedCount === presentedInactive,
           "strip: an INACTIVE strip must NOT emit presented() — it is not what the reader is looking "
           + "at, got " + (harness.stripPresentedCount - presentedInactive) + " emissions")

        // ...and MOUNTING it onto that position IS a presentation: the same rule the two paged
        // surfaces follow in their own onActiveChanged. Without it, switching layout back to Long
        // Strip left the landing page unreported until the reader happened to scroll.
        stripSurface.active = true
        stripSurface._emitPresented()          // the same entry point onActiveChanged's callLater reaches
        ck(harness.stripPresentedCount === presentedInactive + 1,
           "strip: becoming the MOUNTED surface must report the position the reader is now looking at, "
           + "got " + (harness.stripPresentedCount - presentedInactive) + " emissions")
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
        ck(d1 !== null && d1.errorCode === "missing_file",
           "strip-fail: page 1's placard must carry the backend's TYPED code, got '" + (d1 ? d1.errorCode : "<null>") + "'")
        ck(d0 !== null && d0.hasError === false, "strip-fail: page 0 must be UNAFFECTED by page 1's failure")
        // Task 11: the strip mounts THE SAME card the paged surfaces do, and it offers the two ways
        // out. Its own rule is read, never its effective `visible` — an offscreen tree reports every
        // item invisible, so a `visible` assertion here would quietly stop testing anything.
        ck(d1 !== null && d1.errorActionsShown === true,
           "strip-fail: a damaged page in the column must offer Retry/Skip — the dead end Task 11 "
           + "closes, and Long Strip is the layout you meet the most pages in")

        // ...and both actions reach the SURFACE, carrying THIS ROW's page in the 1-based scale the
        // shell speaks. The row matters: in a column the broken page and the page under the viewport
        // centre are routinely not the same one, so a card that reported the centre page would retry
        // a page that is perfectly fine.
        var stripRetried = []
        var stripSkipped = []
        stripFailSurface.retryRequested.connect(function (p) { stripRetried.push(p) })
        stripFailSurface.skipRequested.connect(function (p) { stripSkipped.push(p) })
        var card1 = harness.byName(d1, "retryAction")
        var skip1 = harness.byName(d1, "skipAction")
        ck(card1 !== null && skip1 !== null, "strip-fail: the row's card must carry both actions")
        if (card1) card1.activated()
        if (skip1) skip1.activated()
        ck(JSON.stringify(stripRetried) === "[2]",
           "strip-fail: Retry must raise THIS ROW's page, 1-based (page index 1 -> 2), got " + JSON.stringify(stripRetried))
        ck(JSON.stringify(stripSkipped) === "[2]",
           "strip-fail: Skip must raise THIS ROW's page, 1-based (page index 1 -> 2), got " + JSON.stringify(stripSkipped))

        // A retry clears the surface's OWN failure memo too. Without that the card would sit there
        // through the whole re-read: the strip caches failures locally (the model's role is an int
        // that arrives on its own clock), so the backend's cleared verdict alone is not visible here.
        coreFail.pageRetried(1)
        d1 = stripFailSurface.itemAt(1)
        ck(d1 !== null && d1.hasError === false,
           "strip-fail: pageRetried(1) must take the card down so the placeholder shows while the "
           + "re-read runs")

        coreFail.pageFailed(1, "missing_file")   // ...and it comes back if the re-read fails again
        d1 = stripFailSurface.itemAt(1)
        ck(d1 !== null && d1.hasError === true, "strip-fail: a second failure must show the card again")

        coreFail.pageReady(1)                 // a later successful (re)decode clears the placard
        d1 = stripFailSurface.itemAt(1)
        ck(d1 !== null && d1.hasError === false, "strip-fail: pageReady(1) after a failure must clear page 1's placard")
    }

    // ==================== AUTO-SCROLL (Task 8) ====================
    // Hemanth's rule, verbatim: "Layout and motion remain separate. Long Strip creates the vertical
    // page flow; Auto-scroll only supplies motion at the already chosen width. Starting or resuming
    // Auto-scroll must never resize the page."
    //
    // The drive is a FrameAnimation, which an offscreen harness never ticks, so the motion is driven
    // through _autoScrollTick(ms) — the same function the animation calls, so the tested logic IS
    // the shipped logic (the house pattern _flushViewportReport / _flushEmit already follow).
    property var autoSurface: null
    property int autoEndedCount: 0

    function runStripAutoScroll() {
        // 40 rows x 1200px against a 480px viewport: a long column with real travel in it.
        fillStripModel(stripModelAuto, 40)
        coreAuto.stripModel = stripModelAuto
        autoSurface = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreAuto
        })
        var s = autoSurface
        if (!s) { failures.push("auto: createObject returned null"); return }
        s.autoScrollEnded.connect(function () { harness.autoEndedCount += 1 })
        s.forceRelayout()

        // --- the drive is OFF until the shell says otherwise, and it is the shell's flag ---
        ck(s.autoScrollRunning === false, "auto: the surface must start with the motion OFF")
        ck(s.autoScrollSpeed === 1.0, "auto: the default speed is 1.0, got " + s.autoScrollSpeed)
        ck(Math.abs(s.autoScrollPixelsPerSecond - 120) < 1e-6,
           "auto: 1x is 120 px/s, got " + s.autoScrollPixelsPerSecond)

        // --- POSITIVE MOVEMENT at 1x, and the arithmetic is the stated one ---
        s.contentY = 0
        coreAuto.setStripLayoutCalls = 0
        var widthBefore = coreAuto.lastStripLayout
        s.autoScrollRunning = true
        s._autoScrollTick(16)
        ck(s.contentY > 0, "auto: a tick must advance the column, got contentY " + s.contentY)
        // 16ms at 120px/s = 1.92px. The cold-first-tick guard rounds the FIRST tick up to a whole
        // frame (16.67ms -> 2.0px), which is the documented trap, so this asserts the guarded value.
        ck(Math.abs(s.contentY - 120 * (1000 / 60) / 1000) < 0.01,
           "auto: the first tick travels a WHOLE frame (the cold-tick guard), got " + s.contentY)
        var afterFirst = s.contentY
        s._autoScrollTick(16)
        ck(Math.abs((s.contentY - afterFirst) - 120 * 16 / 1000) < 0.01,
           "auto: a settled tick travels rate x time (1.92px), got " + (s.contentY - afterFirst))

        // --- AUTO-SCROLL NEVER RESIZES THE PAGE. The one call that could is counted. ---
        for (var i = 0; i < 30; i++) s._autoScrollTick(16)
        ck(coreAuto.setStripLayoutCalls === 0,
           "auto: NOTHING on the Auto-scroll path may call setStripLayout — starting or resuming must "
           + "never resize the page. Got " + coreAuto.setStripLayoutCalls + " calls.")
        ck(coreAuto.lastStripLayout === widthBefore,
           "auto: ...and the backend's last layout is untouched by the motion")

        // --- SPEED scales the travel, and nothing else ---
        s.autoScrollSpeed = 2.0
        ck(Math.abs(s.autoScrollPixelsPerSecond - 240) < 1e-6, "auto: 2x is 240 px/s, got " + s.autoScrollPixelsPerSecond)
        s.contentY = 1000
        s._autoScrollTick(100)
        var travel2x = s.contentY - 1000
        s.autoScrollSpeed = 1.0
        s.contentY = 1000
        s._autoScrollTick(100)
        var travel1x = s.contentY - 1000
        ck(Math.abs(travel2x - travel1x * 2) < 0.01,
           "auto: 2x must travel exactly twice as far as 1x in the same time, got " + travel2x + " vs " + travel1x)
        ck(coreAuto.setStripLayoutCalls === 0, "auto: a SPEED change must not resize the page either")
        // the speed is clamped where it is used, so a corrupt value can never stall or bolt
        s.autoScrollSpeed = 99
        ck(Math.abs(s.autoScrollPixelsPerSecond - 360) < 1e-6, "auto: the speed clamps to 3x (360 px/s), got " + s.autoScrollPixelsPerSecond)
        s.autoScrollSpeed = 0
        ck(Math.abs(s.autoScrollPixelsPerSecond - 30) < 1e-6, "auto: the speed clamps UP to 0.25x (30 px/s), got " + s.autoScrollPixelsPerSecond)
        s.autoScrollSpeed = 1.0

        // --- a stalled frame must not TELEPORT the page ---
        s.contentY = 1000
        s._autoScrollTick(100000)          // the clock resumed after a very long stall
        ck(s.contentY - 1000 <= 120 * 0.1 + 0.01,
           "auto: a stalled frame is capped at 100ms of travel, got " + (s.contentY - 1000))

        // --- IT STOPS AT THE END, once, and does not run past it ---
        var maxY = s.contentHeight - 480
        s.contentY = maxY - 1
        harness.autoEndedCount = 0
        s._autoScrollTick(1000)            // a full second: far past the remaining 1px
        ck(Math.abs(s.contentY - maxY) < 0.01,
           "auto: the column must stop exactly at the end, got " + s.contentY + " want " + maxY)
        ck(harness.autoEndedCount === 1, "auto: reaching the end must report ONCE, got " + harness.autoEndedCount)

        // --- TRACKING FOLLOWS THE MOTION. Auto-scroll is real reading: a tick must not be
        //     _programmatic, or the HUD counter and the Continue record would sit still while the
        //     book moved underneath them. ---
        s.contentY = 0
        s._flushEmit()                     // drain anything already scheduled
        s._autoScrollTick(500)
        ck(s._emitPending === true,
           "auto: a tick must SCHEDULE a tracking emit (it is real reading, not a programmatic move)")

        // --- an empty / unscrollable column stops rather than spinning ---
        var tiny = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreAuto
        })
        if (tiny) {
            var tinyEnded = 0
            tiny.autoScrollEnded.connect(function () { tinyEnded += 1 })
            tiny.height = 100000           // viewport taller than the whole book -> nowhere to travel
            tiny.forceRelayout()
            tiny._autoScrollTick(16)
            ck(tinyEnded === 1, "auto: a column with nowhere to travel must report the end, not spin, got " + tinyEnded)
            tiny.destroy()
        }
    }

    // ============= THE RETENTION WINDOW: core.requestRange (Task 8) =============
    // Task 2 built requestRange and left it unwired; this surface is its first production caller.
    // Its doc comment is explicit about the cost: a call whose clamped range DIFFERS walks both
    // cache hashes under both mutexes and frees QImages on the GUI thread, contending the very mutex
    // every provider worker takes for every page fetch. Driving that from a raw per-frame scroll
    // signal is the exact shape of the cascade that turned out to be the video player's stutter.
    //
    // So what this block pins is not "does it call" but HOW OFTEN, and in what units.
    property var rangeSurface: null

    function runStripRequestRange() {
        fillStripModel(stripModelRange, 60)     // 60 rows x 1200px, tops at i*1220
        coreRange.stripModel = stripModelRange
        rangeSurface = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreRange
        })
        var s = rangeSurface
        if (!s) { failures.push("range: createObject returned null"); return }
        s.forceRelayout()
        // Construction reports its own first viewport, which opens the throttle window and seeds the
        // memo. Close the window and reset the memo (entryChanged is the real door for that) so the
        // block below starts from a known state rather than from whatever construction left.
        s._flushRangeWindow()
        coreRange.entryChanged()

        // NOTE on the arithmetic below: the ListView stacks its delegates by HEIGHT (1200 here), so
        // row i is drawn at y = i*1200. fillStripModel's `top` role is the BACKEND's geometry and is
        // deliberately not what the drawn column uses — indexAt reads the drawn column.

        // --- PAGE INDICES, NOT PIXELS. The first report names the visible run as model rows. ---
        coreRange.resetRangeSpy()
        s.contentY = 0
        s.forceRelayout()
        s._flushViewportReport()
        ck(coreRange.requestRangeCalls === 1,
           "range: the first viewport report must sweep exactly once, got " + coreRange.requestRangeCalls)
        var r0 = coreRange.lastRange()
        ck(r0 !== null && r0.first === 0 && r0.last === 0,
           "range: at the top of the book the visible run is page 0..0, got " + JSON.stringify(r0))
        s._flushRangeWindow()      // close the window this opened, so the next block starts clean

        // --- BRAKE 1 + 2: a burst of scroll INSIDE one page never crosses the seam again ---
        // The window is a page range, so scrolling 300px down a 1200px page changes nothing the
        // backend needs to know. This is what makes a 60Hz caller safe at all.
        coreRange.resetRangeSpy()
        for (var i = 1; i <= 10; i++) {
            s.contentY = i * 30
            s.forceRelayout()
            s._flushViewportReport()
            s._flushRangeWindow()
        }
        ck(coreRange.requestRangeCalls === 0,
           "range: ten scroll steps WITHIN one page must not sweep at all (the memo), got "
           + coreRange.requestRangeCalls)

        // --- a real page-range change DOES sweep, exactly once ---
        coreRange.resetRangeSpy()
        s.contentY = 12200          // inside row 10 (12000..13200), and its bottom probe is too
        s.forceRelayout()
        s._flushViewportReport()
        s._flushRangeWindow()
        ck(coreRange.requestRangeCalls === 1,
           "range: crossing into a new page range must sweep once, got " + coreRange.requestRangeCalls)
        var r1 = coreRange.lastRange()
        ck(r1 !== null && r1.first === 10 && r1.last === 10,
           "range: the swept window must be the page indices actually on screen, got " + JSON.stringify(r1))

        // --- BRAKE 3: a FLING coalesces. Many viewport reports inside one throttle window produce
        //     ONE leading sweep, not one per report, however many page boundaries are crossed. ---
        coreRange.resetRangeSpy()
        for (var p = 20; p < 40; p++) {
            s.contentY = p * 1200
            s.forceRelayout()
            s._flushViewportReport()     // the 16ms viewport door — NOT the range window
        }
        ck(coreRange.requestRangeCalls === 1,
           "range: a 20-page fling inside one throttle window must sweep ONCE (leading edge), got "
           + coreRange.requestRangeCalls)
        // ...and nothing is DROPPED: the trailing flush lands the final position.
        s._flushRangeWindow()
        var r2 = coreRange.lastRange()
        ck(coreRange.requestRangeCalls === 2,
           "range: the window closing must land the LAST position, got " + coreRange.requestRangeCalls + " sweeps")
        ck(r2 !== null && r2.first === 39 && r2.last === 39,
           "range: ...and it must be where the fling ENDED, got " + JSON.stringify(r2))
        // a window that closes with nothing waiting stops rather than re-arming forever
        coreRange.resetRangeSpy()
        s._flushRangeWindow()
        ck(coreRange.requestRangeCalls === 0,
           "range: a window closing with nothing pending must sweep nothing, got " + coreRange.requestRangeCalls)

        // --- A FULL AUTO-SCROLL RUN is the case a settle-only debounce would never serve: the
        //     column moves continuously, so "quiet" never arrives. Measured here rather than
        //     asserted loosely — 60 frames of motion inside one page must cost ZERO sweeps. ---
        coreRange.resetRangeSpy()
        s.contentY = 12200
        s.forceRelayout()
        s._flushViewportReport()
        s._flushRangeWindow()
        coreRange.resetRangeSpy()
        s.autoScrollRunning = true
        for (var f = 0; f < 60; f++) {
            s._autoScrollTick(16)          // 60 frames at 120px/s ~= 115px of travel
            s._flushViewportReport()
            s._flushRangeWindow()
        }
        s.autoScrollRunning = false
        ck(coreRange.requestRangeCalls === 0,
           "range: a second of Auto-scroll inside one page must cost ZERO sweeps, got "
           + coreRange.requestRangeCalls)

        // --- an INACTIVE strip owns no window, INCLUDING through its own throttle timer. Both
        //     reading surfaces are mounted at once against ONE backend, and the 250ms window can
        //     close AFTER a layout switch has taken this strip off screen — at which point a sweep
        //     from here would evict what the visible surface is showing. ---
        coreRange.resetRangeSpy()
        s.contentY = 0
        s.forceRelayout()
        s._flushViewportReport()              // active: sweeps (memo was 10..10) and opens the window
        ck(coreRange.requestRangeCalls === 1, "range: fixture - one active sweep before going hidden, got "
           + coreRange.requestRangeCalls)
        s.contentY = 30000
        s.forceRelayout()
        s._flushViewportReport()              // window still open: coalesced, nothing sent
        s.active = false                      // ...and NOW the reader switches layout
        s._flushRangeWindow()                 // the timer fires against a hidden surface
        ck(coreRange.requestRangeCalls === 1,
           "range: an INACTIVE strip must not sweep even when its own throttle window closes, got "
           + coreRange.requestRangeCalls)
        // ...and a plain report from a hidden strip does nothing either
        s.contentY = 0
        s.forceRelayout()
        s._flushViewportReport()
        s._flushRangeWindow()
        ck(coreRange.requestRangeCalls === 1,
           "range: an INACTIVE strip must not sweep from a viewport report either, got "
           + coreRange.requestRangeCalls)
        s.active = true

        // --- a FRESH ENTRY resets the memo. ComicReaderCore resets its own last-swept range on
        //     every openEntry ("a new book always sweeps even if it opens on the page numbers the
        //     last one closed at"); a QML memo that did not reset alongside it would swallow the new
        //     book's first call. ---
        s.contentY = 0
        s.forceRelayout()
        s._flushViewportReport()
        s._flushRangeWindow()
        coreRange.resetRangeSpy()
        s._flushViewportReport()
        s._flushRangeWindow()
        ck(coreRange.requestRangeCalls === 0, "range: precondition — the memo is holding at 0..0")
        coreRange.entryChanged()             // a new book opened on the same page numbers
        s._flushViewportReport()
        ck(coreRange.requestRangeCalls === 1,
           "range: a fresh entry must re-sweep even on the same page numbers, got " + coreRange.requestRangeCalls)
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

    // ===================== UNIFIED PAIR SCALE / SPINE / CENTRING =====================
    //
    // Scanned volumes are trimmed page by page: 1550x2200 next to 1500x2200 is routine. Sizing each
    // half to the SAME half-width renders one page visibly larger than the other — tops aligned,
    // bottoms ragged, art scale jumping across the gutter. Both lineage readers compute ONE scale
    // for the whole displayed unit (Reader 1's computeSpreadLayout: `Math.min` over the two halves'
    // fits, called with fitWidth:true; TB2 ComicReader.cpp "B2: Unified pair scale — both pages at
    // identical heights") and apply it to both halves, so the pages keep their true relative size
    // and meet flush at the spine.
    //
    // WHERE THE NATURAL SIZE COMES FROM, and why this fixture supplies it through pageInfo():
    // the surface caps its decode with `sourceSize.width: srcCapW` (1400 at 100%), and
    // ComicReaderProvider::requestImage answers with the page ALREADY scaled to that width, then
    // reports that scaled size back. So in production BOTH halves of a real pair report
    // implicitWidth == 1400 — a "shared scale" computed off implicitWidth is arithmetically
    // IDENTICAL to sizing each half on its own, i.e. a fix that fixes nothing. The true, uncapped
    // geometry lives in the backend (PageMeta::sourceSize, learned from the file header ahead of
    // the decode) and reaches QML through pageInfo().sourceWidth/sourceHeight — the same source the
    // strip already sizes its column from. The Image's implicit size stays as the FALLBACK, and is
    // covered by runDoubleDecodedPair() below with real pixels.
    function runDoubleUnifiedScale() {
        // 800x480 viewport at 100% -> each half box is 400 wide.
        coreScale.units[20] = { rightIndex: 20, leftIndex: 21, spread: false, coverAlone: false }
        coreScale.pageSizes[20] = { w: 1550, h: 2200 }     // the WIDER trim
        coreScale.pageSizes[21] = { w: 1500, h: 2200 }     // the narrower trim, same height
        var dbl = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 21, "rtl": true, "core": coreScale
        })
        if (!dbl) { failures.push("double-scale: createObject returned null"); return }
        if (!dbl.isPair) { failures.push("double-scale: fixture must be a PAIR"); return }

        var rScale = dbl.rightPageWidth / dbl.rightNaturalWidth
        var lScale = dbl.leftPageWidth / dbl.leftNaturalWidth
        ck(approx(rScale, lScale, 1e-9),
           "double-scale: BOTH halves of a mismatched pair must be drawn at ONE shared scale "
           + "(right " + fx(rScale, 6) + " vs left " + fx(lScale, 6) + ") — sizing each half "
           + "to the same half-width renders one page bigger than the other")
        ck(approx(dbl.rightPageHeight, dbl.leftPageHeight, 0.01),
           "double-scale: two pages of the SAME source height must be drawn at the same height under "
           + "one scale (right " + fx(dbl.rightPageHeight, 2) + " vs left "
           + fx(dbl.leftPageHeight, 2) + ") — this is the ragged-bottom tell")
        // the wider page still fills its half — the shared scale must be the MIN of the two fits,
        // never a shrink of the whole spread
        ck(approx(Math.max(dbl.rightPageWidth, dbl.leftPageWidth), 400, 0.01),
           "double-scale: the WIDER page must still fill its 400px half, got "
           + Math.max(dbl.rightPageWidth, dbl.leftPageWidth))
        ck(dbl.leftPageWidth < dbl.rightPageWidth - 1,
           "double-scale: the narrower-trim page must be drawn genuinely NARROWER (it is 1500 wide "
           + "next to 1550), got left=" + dbl.leftPageWidth + " right=" + dbl.rightPageWidth)

        // --- flush at the spine, in BOTH directions (the centre line is _contentW/2 = 400) ---
        ck(approx(dbl.rightIndexX, 400, 0.01),
           "double-spine RTL: the rightIndex page's INNER edge must sit on the centre line 400, got " + dbl.rightIndexX)
        ck(approx(dbl.leftIndexX + dbl.leftPageWidth, 400, 0.01),
           "double-spine RTL: the leftIndex page's INNER (right) edge must meet the centre line 400, got "
           + (dbl.leftIndexX + dbl.leftPageWidth))
        dbl.rtl = false
        ck(approx(dbl.rightIndexX + dbl.rightPageWidth, 400, 0.01),
           "double-spine LTR: mirrored — the rightIndex page's INNER (right) edge must meet 400, got "
           + (dbl.rightIndexX + dbl.rightPageWidth))
        ck(approx(dbl.leftIndexX, 400, 0.01),
           "double-spine LTR: the leftIndex page's INNER edge must sit on the centre line 400, got " + dbl.leftIndexX)
        dbl.rtl = true

        // --- panYMax is the DRAWN unit's overflow (the shell binds it to vScrollMax to decide
        // whether Up/Down pan). 2200 * (400/1550) = 567.74 in a 480 viewport. ---
        ck(dbl.unitHeight > dbl.height,
           "double-scale precondition: this pair must overflow the viewport (unitHeight " + dbl.unitHeight + ")")
        ck(approx(dbl.panYMax, dbl.unitHeight - dbl.height, 0.01),
           "double-scale: panYMax must be the DRAWN unit's overflow (" + (dbl.unitHeight - dbl.height).toFixed(2)
           + "), got " + dbl.panYMax)
        dbl.destroy()
    }

    // A pair SHORTER than the viewport: the block centres instead of hanging off the top with all
    // the black below, each half centres inside the pair's band, and the gutter shadow rides the
    // pages rather than running the full window height through empty black.
    function runDoubleCentring() {
        coreShort.units[30] = { rightIndex: 30, leftIndex: 31, spread: false, coverAlone: false }
        coreShort.pageSizes[30] = { w: 1550, h: 600 }      // tallest half:  600 * (400/1550) = 154.8
        coreShort.pageSizes[31] = { w: 1500, h: 400 }      // shorter half:  400 * (400/1550) = 103.2
        var dbl = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 31, "rtl": true, "core": coreShort
        })
        if (!dbl) { failures.push("double-centre: createObject returned null"); return }
        ck(dbl.unitHeight < dbl.height,
           "double-centre precondition: this pair must FIT the viewport (unitHeight " + dbl.unitHeight
           + " vs height " + dbl.height + ")")
        ck(dbl.unitTop > 0,
           "double-centre: a pair shorter than the viewport must be VERTICALLY CENTRED, not pinned to "
           + "the top with all the black below, got unitTop " + dbl.unitTop)
        ck(approx(dbl.unitTop, (dbl.height - dbl.unitHeight) / 2, 0.01),
           "double-centre: the block's top offset must be half the slack, got " + dbl.unitTop
           + " want " + ((dbl.height - dbl.unitHeight) / 2))
        ck(approx(dbl.panYMax, 0, 1e-9),
           "double-centre: a pair that FITS has no vertical pan headroom (the shell's vScrollMax), got " + dbl.panYMax)

        // each half centres inside the pair's band — a shorter page sits mid-height, not top-aligned
        ck(approx(dbl.rightIndexY, dbl.unitTop, 0.01),
           "double-centre: the TALLEST half fills the band, so it starts at the block top " + dbl.unitTop
           + ", got " + dbl.rightIndexY)
        ck(dbl.leftIndexY > dbl.unitTop + 1,
           "double-centre: the SHORTER half must sit mid-height inside the pair's band, not top-aligned "
           + "(y " + dbl.leftIndexY + " vs block top " + dbl.unitTop + ")")
        ck(approx(dbl.leftIndexY, dbl.unitTop + (dbl.unitHeight - dbl.leftPageHeight) / 2, 0.01),
           "double-centre: the shorter half must be centred in the band, got " + dbl.leftIndexY
           + " want " + (dbl.unitTop + (dbl.unitHeight - dbl.leftPageHeight) / 2))

        // --- the gutter shadow follows the PAGES, not the viewport ---
        ck(approx(dbl.gutterShadowItem.height, dbl.unitHeight, 0.01),
           "double-gutter: the spine shadow's height must be the DRAWN PAIR height " + dbl.unitHeight
           + ", not the viewport height " + dbl.height + ", got " + dbl.gutterShadowItem.height)
        ck(approx(dbl.gutterShadowItem.y, dbl.unitTop, 0.01),
           "double-gutter: the spine shadow must ride the pair's vertical offset " + dbl.unitTop
           + " (else it darkens empty black above and below the pages), got " + dbl.gutterShadowItem.y)
        dbl.destroy()
    }

    // A ZOOM STEP MUST KEEP YOUR PLACE. setZoom() used to slam both pan axes to 0, so panning across
    // to read a panel at 200% and pressing Ctrl+= once more teleported you to the corner of the
    // spread — in RTL manga, the far end of the page you were reading. Both lineage readers clamp the
    // existing pan into the new bounds and never zero it (Tankoban 2 ComicReader.cpp applyPan, which
    // only ever clamps; Colosseum Reader 1 MangaReader.qml zoomBy, which calls clampPan() and nothing
    // else). Pan is reset by a UNIT CHANGE — a new spread — not by a zoom step; that contract is
    // asserted in runDouble() and stays.
    //
    // The fixture is a TALL mismatched pair with real backend geometry, so BOTH axes have genuine
    // headroom and neither assertion can pass by accident. That matters for panY especially: since
    // the geometry rework, panYMax is the DRAWN unit's overflow, so it scales with zoom AND stays
    // above zero at 100% for any spread taller than the viewport. A pair with no backend geometry
    // (the older double fixtures) falls back to _rightH == the viewport height, which makes panYMax
    // identically 0 at every zoom and would make the vertical half of this test vacuous.
    function runDoubleZoomKeepsPan() {
        // 800x480 viewport; both halves 2200 tall -> at 100% the pair draws 567.74 tall and OVERFLOWS.
        coreZoomPan.units[50] = { rightIndex: 50, leftIndex: 51, spread: false, coverAlone: false }
        coreZoomPan.pageSizes[50] = { w: 1550, h: 2200 }
        coreZoomPan.pageSizes[51] = { w: 1500, h: 2200 }
        var dbl = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 51, "rtl": true, "core": coreZoomPan
        })
        if (!dbl) { failures.push("zoom-pan: createObject returned null"); return }
        ck(dbl.isPair, "zoom-pan: fixture must be a PAIR")

        // --- read a panel at 200%, then take one more zoom step IN (the bounds GROW) ---
        dbl.setZoom(200)
        dbl.panBy(120, 80)
        var keepX = dbl.panX, keepY = dbl.panY
        ck(keepX > 0 && keepY > 0,
           "zoom-pan precondition: a pan must apply on BOTH axes at 200%, got x=" + keepX + " y=" + keepY)

        dbl.setZoom(220)
        ck(dbl.panX > 0 && approx(dbl.panX, Math.min(keepX, dbl.panXMax), 1e-9),
           "zoom-pan: a zoom step must KEEP the horizontal pan (clamped into the new bounds), want "
           + Math.min(keepX, dbl.panXMax) + " got " + dbl.panX
           + " — zeroing it teleports you to the corner of the spread mid-panel")
        ck(dbl.panY > 0 && approx(dbl.panY, Math.min(keepY, dbl.panYMax), 1e-9),
           "zoom-pan: a zoom step must KEEP the vertical pan too (clamped into the new bounds), want "
           + Math.min(keepY, dbl.panYMax) + " got " + dbl.panY)

        // --- now zoom OUT from the far corner: this is where the clamp actually has to bite, and
        // where "keep it" would otherwise leave the pan outside the smaller bounds ---
        dbl.panBy(99999, 99999)
        var farX = dbl.panX, farY = dbl.panY
        ck(approx(farX, dbl.panXMax, 1e-9) && approx(farY, dbl.panYMax, 1e-9),
           "zoom-pan precondition: parked at the far corner of the 220% spread, got x=" + farX + " y=" + farY)

        dbl.setZoom(120)
        ck(dbl.panX > 0 && dbl.panX < farX && approx(dbl.panX, dbl.panXMax, 1e-9),
           "zoom-pan: zooming OUT must CLAMP the kept horizontal pan down to the smaller panXMax ("
           + dbl.panXMax + "), not zero it and not leave it out of bounds at " + farX + ", got " + dbl.panX)
        ck(dbl.panY > 0 && dbl.panY < farY && approx(dbl.panY, dbl.panYMax, 1e-9),
           "zoom-pan: zooming OUT must CLAMP the kept vertical pan down to the smaller panYMax ("
           + dbl.panYMax + "), not zero it, got " + dbl.panY)

        // --- back to 100%. panX lands on 0, but it must land there BECAUSE the clamp has nowhere
        // left to go, not because setZoom zeroes anything: at 100% the content is exactly the
        // viewport width, so panXMax IS 0. panY is the proof that nothing is being zeroed — this
        // spread is still taller than the viewport at 100%, so vertical headroom REMAINS and the
        // kept pan must survive, clamped. ---
        dbl.setZoom(100)
        ck(approx(dbl.panXMax, 0, 1e-9),
           "zoom-pan: at 100% the content is exactly the viewport width, so panXMax must be 0 (this is "
           + "WHY panX lands on 0 below), got " + dbl.panXMax)
        ck(dbl.panX === 0,
           "zoom-pan: back at 100% the clamp alone must take panX to 0 (panXMax is 0 there), got " + dbl.panX)
        ck(dbl.panYMax > 0,
           "zoom-pan precondition: this pair is TALLER than the viewport at 100% (drawn "
           + dbl.unitHeight.toFixed(2) + " vs " + dbl.height + "), so vertical headroom must REMAIN, got panYMax "
           + dbl.panYMax)
        ck(dbl.panY > 0 && approx(dbl.panY, dbl.panYMax, 1e-9),
           "zoom-pan: back at 100% a spread that still overflows must KEEP its vertical pan, clamped to "
           + "panYMax (" + dbl.panYMax + ") — landing on 0 here is the tell that setZoom is zeroing "
           + "rather than clamping, got " + dbl.panY)

        // --- and the reset that MUST still happen: a new unit ---
        dbl.setZoom(200)
        dbl.panBy(120, 80)
        coreZoomPan.units[52] = { rightIndex: 52, leftIndex: 53, spread: false, coverAlone: false }
        dbl.currentPage = 53
        ck(dbl.panX === 0 && dbl.panY === 0,
           "zoom-pan: a UNIT change must still reset pan to the origin (only zoom survives a turn), got x="
           + dbl.panX + " y=" + dbl.panY)
        dbl.destroy()
    }

    // ---- FALLBACK path, then the OVERRIDE. Two PNG fixtures with genuinely different dimensions
    // (200x300 and 150x300) that the Image really loads, so the fallback runs on the decoder's own
    // implicitWidth/implicitHeight rather than on numbers this harness injected.
    //
    // WHY THE FIXTURES ARE FILES, PRE-WARMED BY warmRight/warmLeft, AND WHY THIS PHASE IS
    // SYNCHRONOUS. This used to hand the surface two data: URLs and poll for the decode, and it was
    // the flakiest thing in the suite — two brothers hit it independently, one 2 failures in 3.
    // MEASURED 2026-07-26, because the obvious reads were all wrong: the decode is not slow, data:
    // URLs are not unreliable, the decode cap's upscale is not the cost, and the poll interval was
    // not the cause either (each was ruled out by its own controlled run). Qt loads an
    // `asynchronous: true` Image on the shared QQuickPixmapReader thread, and Qt runs that thread at
    // LOW priority — so on a machine with other work on it, it simply does not get scheduled. The
    // failing runs showed `progress` still 0 after NINE seconds, i.e. the job had never been picked
    // up at all, and a plain control Image in the same process stalled in the same millisecond, so
    // the stall is process-wide rather than anything about these two images. Re-running this very
    // gate with nothing changed but the process priority raised to High turned "292-2100ms, or never"
    // into a flat 190-240ms. A harness cannot fix that thread, and no budget is safe against it —
    // raising it (3s -> 6s) was already tried and the gate still failed 6 runs in 10 under load.
    //
    // So the wall clock is designed out rather than tuned. A local FILE fixture loaded by an
    // `asynchronous: false` Image is decoded synchronously on THIS thread, and it lands in the same
    // QQuickPixmapCache the surface reads from: same url, same sourceSize (the surface's srcCapW is
    // 1400 at 100%) and same PreserveAspectFit make the same cache key, so the surface's own
    // `asynchronous: true` Images hit the cache and report their implicit size immediately, with the
    // reader thread never involved. Measured: the whole phase is ~15ms, and 10/10 under the same
    // load that failed 6/10 before. Do NOT reintroduce a poll or a budget here. If someone changes
    // srcCapW or the fillMode the key stops matching, the images go asynchronous again and the
    // precondition below fails loudly — by name — instead of flaking.
    //
    // MEASURED HERE, and the reason the backend's geometry is the PRIMARY source: with the surface's
    // decode cap in play these two visibly different pages do NOT report their own widths — the
    // loader hands back both normalised to the cap (1400x2100 and 1400x2800). So the fallback keeps
    // the unit drawable and flush, but it cannot recover the two pages' true RELATIVE size. Part 2
    // proves the backend's header geometry overrides a fully decoded Image and restores it.
    readonly property url png200x300: Qt.resolvedUrl("comicreader_fixtures/page-200x300.png")
    readonly property url png150x300: Qt.resolvedUrl("comicreader_fixtures/page-150x300.png")

    // The synchronous pre-warm. `asynchronous: false` on a local file decodes on the GUI thread;
    // every other property here exists ONLY to mirror the surface's own page Images, because the
    // QQuickPixmap cache key is (url, requestSize, aspect-ratio options) — miss any of them and the
    // surface re-requests through the reader thread and the determinism is gone.
    Image {
        id: warmRight
        source: harness.png200x300
        asynchronous: false; cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 1400          // ComicReaderDoubleSurface.srcCapW at zoomPercent 100
        visible: false
    }
    Image {
        id: warmLeft
        source: harness.png150x300
        asynchronous: false; cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 1400
        visible: false
    }
    // ...and the Single surface's PREVIEW tier, which asks for half the hq cap (700 at zoom 100). A
    // different requestSize is a different QQuickPixmap cache key, so it needs its own warm entry or
    // the presented() scenario would wait on Qt's reader thread.
    Image {
        id: warmSinglePreview
        source: harness.png200x300
        asynchronous: false; cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 700
        visible: false
    }
    // ...and the same fixture at the OTHER preview width, so a Single page turn between the two files
    // is a pure cache hit at both request sizes — which is what makes the cached-navigation assertion
    // in runSinglePresented test the case it claims to.
    Image {
        id: warmSinglePreviewB
        source: harness.png150x300
        asynchronous: false; cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 700
        visible: false
    }

    function runDoubleDecodedPair() {
        ck(warmRight.status === Image.Ready && warmLeft.status === Image.Ready,
           "double-decoded precondition: the two file fixtures must decode SYNCHRONOUSLY into the "
           + "pixmap cache (asynchronous: false on a local file), so this phase never waits on Qt's "
           + "low-priority reader thread — got warm status right " + warmRight.status
           + " left " + warmLeft.status)

        coreDecoded.units[40] = { rightIndex: 40, leftIndex: 41, spread: false, coverAlone: false }
        coreDecoded.pageUrls[40] = png200x300
        coreDecoded.pageUrls[41] = png150x300
        // NOTE: coreDecoded.pageSizes stays EMPTY — the backend knows no geometry here, which is
        // exactly the fallback under test.
        var dbl = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 41, "rtl": true, "core": coreDecoded
        })
        if (!dbl) { failures.push("double-decoded: createObject returned null"); return }

        // --- part 1: no backend geometry -> the Image's own decoded size carries the layout ---
        ck(dbl.rightNaturalWidth > 0 && dbl.rightNaturalHeight > 0 &&
           dbl.leftNaturalWidth > 0 && dbl.leftNaturalHeight > 0,
           "double-decoded: with no backend geometry the natural size must FALL BACK to the Image's "
           + "own decoded implicit size, got right " + dbl.rightNaturalWidth + "x" + dbl.rightNaturalHeight
           + " left " + dbl.leftNaturalWidth + "x" + dbl.leftNaturalHeight)
        var rScale = dbl.rightPageWidth / dbl.rightNaturalWidth
        var lScale = dbl.leftPageWidth / dbl.leftNaturalWidth
        ck(approx(rScale, lScale, 1e-9),
           "double-decoded: on the fallback path the two REAL Images must still be drawn at one shared "
           + "scale (right " + rScale + " vs left " + lScale + ")")
        ck(approx(dbl.rightIndexX, 400, 0.01) && approx(dbl.leftIndexX + dbl.leftPageWidth, 400, 0.01),
           "double-decoded: both halves must still meet the spine at 400, got right " + dbl.rightIndexX
           + " left-inner " + (dbl.leftIndexX + dbl.leftPageWidth))

        // --- part 2: the backend learns the pages' TRUE header geometry. It must OVERRIDE the
        // implicit size of an Image that is already fully decoded — that is the whole point: the
        // decoded size is capped/normalised by `sourceSize.width`, the header size is not. ---
        coreDecoded.pageSizes[40] = { w: 1550, h: 2200 }
        coreDecoded.pageSizes[41] = { w: 1500, h: 2200 }
        coreDecoded.emitPageReady(40)          // the same signal the real core fires; bumps readyRev
        ck(approx(dbl.rightNaturalWidth, 1550) && approx(dbl.leftNaturalWidth, 1500),
           "double-decoded: the BACKEND's header geometry must beat a decoded Image's implicit size "
           + "(the decoded size is normalised by the decode cap, so two differently-trimmed scans both "
           + "report the same width and their true relative size is lost), got right "
           + dbl.rightNaturalWidth + " left " + dbl.leftNaturalWidth)
        ck(approx(dbl.rightPageWidth, 400, 0.01) && approx(dbl.leftPageWidth, 1500 * 400 / 1550, 0.01),
           "double-decoded: with the true sizes the halves must be drawn 400 and "
           + (1500 * 400 / 1550).toFixed(2) + " wide, got " + dbl.rightPageWidth + " and " + dbl.leftPageWidth)
        ck(approx(dbl.rightPageHeight, dbl.leftPageHeight, 0.01),
           "double-decoded: with the true sizes both halves must be drawn at the SAME height, got "
           + dbl.rightPageHeight + " vs " + dbl.leftPageHeight)
        dbl.destroy()
    }

    // ==================== PAIR PRESENTATION GATE (Task 4) ====================
    // The defect: this surface bound each half straight to the stage, so whichever page decoded first
    // appeared and the other stayed black — a spread arriving as one page plus a hole.
    //
    // THIS SCENARIO SERVES REAL FILE FIXTURES, and that is the point. An earlier version drove the gate
    // with provider urls that never resolve offscreen, so no half ever had pixels and the assertions
    // could only ever see the BACKEND's verdict. That is exactly the blind spot that let the real
    // defect survive: the C++ verdict flips to "ready" a beat BEFORE either half has pixels, because
    // each half's pixels come back through a separate async provider round trip and the very pageReady
    // that flips the verdict is what re-points the second half's Image at its bumped url. A test that
    // cannot tell "decoded" from "on screen" cannot see that window at all.
    function runDoublePairGate() {
        ck(warmRight.status === Image.Ready && warmLeft.status === Image.Ready,
           "pair-gate precondition: both file fixtures must be warm in the pixmap cache")

        coreGate.units[2] = { rightIndex: 2, leftIndex: 3, spread: false, coverAlone: false }
        coreGate.pageSizes[2] = { w: 200, h: 300 }
        coreGate.pageSizes[3] = { w: 150, h: 300 }
        // page 2 has real pixels; page 3 has a provider url that never resolves offscreen, which is
        // precisely "this half has not come back yet".
        coreGate.pageUrls[2] = harness.png200x300
        coreGate.pageStates[2] = "ready"
        coreGate.pageStates[3] = "waiting"

        var dbl = makeGate({ "currentPage": 3 })
        if (!dbl) { failures.push("pair-gate: createObject returned null"); return }
        dbl.presented.connect(function (pg, f) {
            harness.doublePresentedCount += 1
            harness.doublePresentedPage = pg
            harness.doublePresentedFrac = f
        })

        ck(dbl.isPair === true, "pair-gate precondition: the unit under test must be a real pair")
        ck(dbl.rightHalfResolved === true && dbl.leftHalfResolved === false,
           "pair-gate precondition: exactly ONE half has pixels (right=" + dbl.rightHalfResolved
           + " left=" + dbl.leftHalfResolved + ")")
        ck(dbl.presentationState === "waiting",
           "pair-gate: with one half undecoded the BACKEND says waiting, got '" + dbl.presentationState + "'")
        ck(dbl.pairVisible === false,
           "pair-gate: THE DEFECT — a half-decoded pair must paint NOTHING (one decoded page beside a "
           + "black rectangle is what this gate exists to prevent), got pairVisible=" + dbl.pairVisible)
        ck(dbl.placeholdersShown === true,
           "pair-gate: while the unit is unpaintable it must show its restrained placeholder instead")
        ck(dbl.rightErrorVisible === false && dbl.leftErrorVisible === false,
           "pair-gate: waiting is NOT an error — no placard while a page can still arrive")

        // ---- THE WINDOW THE VERDICT LEFT OPEN. The backend now reports the WHOLE unit ready — but
        // the second half's pixels have not arrived, because that is a separate provider round trip.
        // Gated on the verdict, this is the frame that paints one page beside a black rectangle. ----
        coreGate.pageStates[3] = "ready"
        coreGate.emitPageReady(3)
        ck(dbl.presentationState === "ready",
           "pair-gate precondition: the backend verdict must now be ready, got '" + dbl.presentationState + "'")
        ck(dbl.leftHalfResolved === false,
           "pair-gate precondition: ...while the second half still has NO pixels, got leftHalfResolved="
           + dbl.leftHalfResolved)
        ck(dbl.pairVisible === false,
           "pair-gate: THE VERDICT IS NOT THE PAINT RULE — the backend saying 'ready' must NOT open the "
           + "gate while a half is still without pixels, or the reader gets one page beside black in the "
           + "beat before they arrive, got pairVisible=" + dbl.pairVisible)
        ck(dbl.placeholdersShown === true,
           "pair-gate: and the placeholder must still be standing in for the unit, got "
           + dbl.placeholdersShown)
        ck(harness.doublePresentedCount === 0,
           "pair-gate: nothing is presented while the unit is not on screen, got "
           + harness.doublePresentedCount)

        // ---- BOTH placeholders, not just the unresolved half's. This is the one place a reviewer
        // prescription and the gate pull in opposite directions, so it is pinned rather than assumed:
        // driving each half's panel off THAT half's own resolution — which is right if each half
        // paints independently — is WRONG under an all-or-nothing gate. The right half here HAS
        // pixels and is still hidden by the gate, so taking its panel away leaves its box as bare
        // black stage: the exact artefact the gate exists to prevent, reintroduced by the stand-in
        // instead of by the page. The panels are the UNIT's stand-in, so they live and die with it. ----
        ck(dbl.rightHalfResolved === true && dbl.leftHalfResolved === false,
           "pair-gate precondition: exactly one half is resolved here (right=" + dbl.rightHalfResolved
           + " left=" + dbl.leftHalfResolved + ") — otherwise the per-half check below is vacuous")
        ck(dbl.rightPlaceholderShown === true,
           "pair-gate: the RESOLVED half must KEEP its placeholder while the unit is gated shut — the "
           + "gate hides its page, so dropping the panel shows bare black in its place")
        ck(dbl.leftPlaceholderShown === true,
           "pair-gate: ...and the unresolved half keeps its own, got " + dbl.leftPlaceholderShown)

        // ---- the second half's pixels arrive: NOW it paints, as one thing ----
        coreGate.pageUrls[3] = harness.png150x300
        coreGate.emitPageReady(3)
        ck(dbl.leftHalfResolved === true,
           "pair-gate precondition: the second half now has real pixels, got " + dbl.leftHalfResolved)
        ck(dbl.pairVisible === true, "pair-gate: a unit whose every half is on screen must paint")
        ck(dbl.placeholdersShown === false, "pair-gate: a painted unit must drop its placeholder")
        ck(dbl.rightImageVisible === true && dbl.leftImageVisible === true,
           "pair-gate: BOTH halves of a painted pair must be drawn")
        // presented() is deferred on the change path; the status change itself notices it immediately.
        ck(harness.doublePresentedCount === 1,
           "pair-gate: the unit becoming visible must report presented() exactly once, got "
           + harness.doublePresentedCount)
        ck(harness.doublePresentedPage === 3 && approx(harness.doublePresentedFrac, 0),
           "pair-gate: presented() must carry the unit's ANCHOR page and a 0 fraction, got page "
           + harness.doublePresentedPage + " frac " + fx(harness.doublePresentedFrac, 2))

        // ---- A CACHED TURN. Both halves of the next unit are already warm, so neither Image.status
        // dips and the derived predicate never changes — only the explicit re-check on the unit-change
        // path can notice this, which is the hole the first review round found. ----
        coreGate.units[6] = { rightIndex: 6, leftIndex: 7, spread: false, coverAlone: false }
        coreGate.pageSizes[6] = { w: 200, h: 300 }
        coreGate.pageSizes[7] = { w: 150, h: 300 }
        coreGate.pageUrls[6] = harness.png200x300
        coreGate.pageUrls[7] = harness.png150x300
        coreGate.pageStates[6] = "ready"
        coreGate.pageStates[7] = "ready"
        harness.doublePresentedCount = 0
        dbl.currentPage = 7
        ck(dbl.pairVisible === true,
           "pair-gate precondition: a fully warm unit paints immediately on the turn, got "
           + dbl.pairVisible)
        ck(harness.doublePresentedCount === 0,
           "pair-gate: the re-check is DEFERRED, so nothing fires synchronously on the turn, got "
           + harness.doublePresentedCount)
        dbl._notePresented()     // the same entry point callLater reaches
        ck(harness.doublePresentedCount === 1 && harness.doublePresentedPage === 7,
           "pair-gate: a turn onto an ALREADY-CACHED unit must still report presented() — no status "
           + "dips, so nothing else can notice it, got count=" + harness.doublePresentedCount
           + " page=" + harness.doublePresentedPage)

        // ---- ONE PARTNER FAILS TERMINALLY. The unit must stop waiting — but NOT before the surviving
        // half has pixels, which is the second face of the same bug: "error" opens the backend verdict,
        // so gating on it took BOTH placeholders away the instant either member failed, leaving an error
        // placard beside a plain black rectangle. ----
        coreGate.units[10] = { rightIndex: 10, leftIndex: 11, spread: false, coverAlone: false }
        coreGate.pageSizes[10] = { w: 200, h: 300 }
        coreGate.pageSizes[11] = { w: 150, h: 300 }
        coreGate.pageStates[10] = "ready"          // the good half, pixels NOT yet served
        coreGate.pageStates[11] = "error"
        coreGate.pageErrors[11] = "decode_failed"
        dbl.currentPage = 11
        coreGate.pageFailed(11, "decode_failed")
        ck(dbl.presentationState === "error",
           "pair-gate: a terminal failure in either half makes the UNIT an error, got '"
           + dbl.presentationState + "'")
        ck(dbl.leftHalfResolved === true,
           "pair-gate: a terminally failed half IS resolved — its placard is the honest answer")
        ck(dbl.rightHalfResolved === false,
           "pair-gate precondition: the surviving half has no pixels yet, got " + dbl.rightHalfResolved)
        ck(dbl.pairVisible === false,
           "pair-gate: an error verdict must NOT open the gate while the SURVIVING half is still "
           + "decoding — that paints a placard beside black, got pairVisible=" + dbl.pairVisible)
        ck(dbl.placeholdersShown === true,
           "pair-gate: and the placeholder must NOT be taken away by a failure in the other half — "
           + "losing it is what made that frame worse than the one before it, got "
           + dbl.placeholdersShown)

        // ...and once the good half is on screen, the unit paints: good page + explicit error side.
        coreGate.pageUrls[10] = harness.png200x300
        coreGate.emitPageReady(10)
        ck(dbl.pairVisible === true,
           "pair-gate: with the surviving half on screen the error unit paints, got " + dbl.pairVisible)
        ck(dbl.placeholdersShown === false, "pair-gate: ...and drops the placeholder")
        ck(dbl.leftErrorVisible === true, "pair-gate: the FAILED half must carry the typed placard")
        ck(dbl.leftErrorCode === "decode_failed",
           "pair-gate: the placard must be TYPED with the backend's code, got '" + dbl.leftErrorCode + "'")
        ck(dbl.rightErrorVisible === false && dbl.rightImageVisible === true,
           "pair-gate: the GOOD half keeps drawing its page, never inherits its partner's placard")

        // ---- Task 11: the damaged half's two ways out, and WHICH half they name ----
        // A pair can show two of these cards, and the good side must never be the one retried. That
        // is the entire reason the card carries its own page rather than the unit's anchor.
        ck(dbl.leftErrorActionsShown === true,
           "pair-gate: the failed half's card must offer Retry/Skip, got " + dbl.leftErrorActionsShown)
        ck(dbl.rightErrorActionsShown === false,
           "pair-gate: the GOOD half must offer nothing — there is nothing wrong with it, got "
           + dbl.rightErrorActionsShown)
        var pairRetried = []
        var pairSkipped = []
        dbl.retryRequested.connect(function (p) { pairRetried.push(p) })
        dbl.skipRequested.connect(function (p) { pairSkipped.push(p) })
        var leftCard = harness.byName(dbl, "leftUnitError")
        var leftRetry = leftCard ? harness.byName(leftCard, "retryAction") : null
        var leftSkip = leftCard ? harness.byName(leftCard, "skipAction") : null
        ck(leftRetry !== null && leftSkip !== null, "pair-gate: the failed half's card must carry both actions")
        if (leftRetry) leftRetry.activated()
        if (leftSkip) leftSkip.activated()
        // Page index 11 is the LEFT half; 10 is the good right one. Asserting 12 (1-based) rather
        // than merely "something" is what catches a card wired to the unit anchor instead of itself.
        ck(JSON.stringify(pairRetried) === "[12]",
           "pair-gate: Retry must name the BROKEN half, 1-based (index 11 -> 12), got " + JSON.stringify(pairRetried))
        ck(JSON.stringify(pairSkipped) === "[12]",
           "pair-gate: Skip must name the BROKEN half, 1-based (index 11 -> 12), got " + JSON.stringify(pairSkipped))

        // ---- and it heals: a page that comes back returns the unit to normal ----
        coreGate.pageStates[11] = "ready"
        delete coreGate.pageErrors[11]
        coreGate.pageUrls[11] = harness.png150x300
        coreGate.emitPageReady(11)
        ck(dbl.presentationState === "ready" && dbl.leftErrorVisible === false
               && dbl.leftImageVisible === true && dbl.pairVisible === true,
           "pair-gate: a healed page clears the unit's error and the half goes back to painting (state='"
           + dbl.presentationState + "' placard=" + dbl.leftErrorVisible + " paints=" + dbl.pairVisible + ")")

        // ---- A NEW BOOK opening on the anchor the last one was left on. Same hole as the Single
        // surface's, and the one that depends on NO cache behaviour: the marker still names this
        // anchor, so without a reset on entryChanged the first unit of book B is never reported. ----
        harness.doublePresentedCount = 0
        ck(dbl._presentedAnchor === 11,
           "pair-gate precondition: the marker must be parked on the current anchor before the new "
           + "book, got " + dbl._presentedAnchor)
        coreGate.entryChanged()               // exactly what the real core emits from openEntry
        ck(dbl._presentedAnchor === -1 || harness.doublePresentedCount > 0,
           "pair-gate: entryChanged must clear the presented marker, got _presentedAnchor="
           + dbl._presentedAnchor)
        dbl._notePresented()                  // the same entry point callLater reaches
        ck(harness.doublePresentedCount === 1 && harness.doublePresentedPage === 11,
           "pair-gate: a NEW BOOK opening on the same anchor must still report presented() — the "
           + "marker is per-book, not for all time, got count=" + harness.doublePresentedCount
           + " page=" + harness.doublePresentedPage)

        // ---- an ABSENT seam no longer decides anything: the gate reads PIXELS, so a core without
        // presentationForPage paints exactly when its pages are on screen. ----
        coreStub.units[2] = { rightIndex: 2, leftIndex: 3, spread: false, coverAlone: false }
        coreStub.pageSizes[2] = { w: 200, h: 300 }
        coreStub.pageSizes[3] = { w: 150, h: 300 }
        coreStub.pageUrls[2] = harness.png200x300
        coreStub.pageUrls[3] = harness.png150x300
        var stub = doubleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 3, "rtl": true, "core": coreStub
        })
        if (stub) {
            ck(stub.presentationState === "ready",
               "pair-gate: a core WITHOUT presentationForPage must read as ready rather than a "
               + "misleading 'waiting', got '" + stub.presentationState + "'")
            ck(stub.pairVisible === true,
               "pair-gate: ...and it must paint, because both halves have pixels, got " + stub.pairVisible)
            stub.destroy()
        } else {
            failures.push("pair-gate: stub createObject returned null")
        }
        dbl.destroy()
    }

    function makeGate(cfg) {
        var base = { "width": 800, "height": 480, "active": true, "rtl": true, "core": coreGate }
        for (var k in cfg) base[k] = cfg[k]
        return doubleComp.createObject(harness, base)
    }

    // ==================== SINGLE PAGE (Task 4) ====================
    // The third layout. A 1000x1500 page in an 800x480 frame: CONTAIN fits it by HEIGHT
    // (min(800/1000, 480/1500) = 0.32), so the whole page is visible at 320x480 and centred — which
    // is the one place Single deliberately differs from the Pair surface's fit-by-width.
    function runSingle() {
        coreSingle.pageSizes[3] = { w: 1000, h: 1500 }
        var sgl = singleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 4, "core": coreSingle
        })
        if (!sgl) { failures.push("single: createObject returned null"); return }

        // --- CONTAIN, centred, whole page on screen ---
        ck(approx(sgl.drawnWidth, 320, 0.01) && approx(sgl.drawnHeight, 480, 0.01),
           "single: a 1000x1500 page in 800x480 must be CONTAIN-fitted to 320x480 (the WHOLE page "
           + "visible, not fit-to-width), got " + fx(sgl.drawnWidth, 2) + "x" + fx(sgl.drawnHeight, 2))
        ck(approx(sgl.drawnX, 240, 0.01) && approx(sgl.drawnY, 0, 0.01),
           "single: the page must be CENTRED in the frame, got x=" + fx(sgl.drawnX, 2) + " y=" + fx(sgl.drawnY, 2))
        ck(approx(sgl.panXMax, 0) && approx(sgl.panYMax, 0),
           "single: a contain-fitted page at 100% has NO pan headroom (that is what contain means), got "
           + fx(sgl.panXMax, 2) + "/" + fx(sgl.panYMax, 2))

        // --- TWO TIERS, stacked: preview lands first, hq fades over it ---
        ck(String(sgl.previewSource).indexOf("tier=preview") >= 0,
           "single: the under-layer must request the PREVIEW tier, got '" + sgl.previewSource + "'")
        ck(String(sgl.hqSource).indexOf("tier=hq") >= 0,
           "single: the over-layer must request the HQ tier, got '" + sgl.hqSource + "'")
        ck(String(sgl.previewSource) !== String(sgl.hqSource),
           "single: the two tiers must be two DIFFERENT requests, not the same url twice")

        // --- the decode-refresh dependency, same as the other two surfaces ---
        var rr0 = sgl.readyRev
        coreSingle.emitPageReady(3)
        ck(sgl.readyRev === rr0 + 1, "single: pageReady must bump readyRev, got " + sgl.readyRev)
        ck(String(sgl.hqSource).indexOf("rev=1") >= 0,
           "single: after pageReady the source must re-evaluate to the bumped ?rev= (page would stay "
           + "BLANK otherwise), got '" + sgl.hqSource + "'")

        // --- ZOOM PRESERVES THE CENTROID. At 200% the page is 640x960: what sat under the middle of
        // the window is still under it, so the fit-page centre stays centred. ---
        sgl.setZoom(200)
        ck(sgl.zoomPercent === 200, "single: setZoom(200) must take, got " + sgl.zoomPercent)
        ck(approx(sgl.drawnHeight, 960, 0.01) && approx(sgl.panYMax, 480, 0.01),
           "single: at 200% the page is 960 tall so panYMax is 480, got h=" + fx(sgl.drawnHeight, 2)
           + " panYMax=" + fx(sgl.panYMax, 2))
        ck(approx(sgl.panY, 240, 0.01),
           "single: zooming from a centred page must KEEP it centred (panY 240 of 480), got " + fx(sgl.panY, 2))

        // ...and from a NON-centred position. Park at the bottom (panY 480 => the page point 0.75 is
        // under the viewport centre), then zoom to 260%: the page is 1248 tall, so holding 0.75 needs
        // panY 696. A clamp-only zoom — what the Pair surface does — would leave panY at 480, so this
        // assertion is what actually distinguishes the two behaviours.
        sgl.panBy(0, 99999)
        ck(approx(sgl.panY, 480, 0.01), "single precondition: pan must clamp to panYMax 480, got " + fx(sgl.panY, 2))
        sgl.setZoom(260)
        ck(approx(sgl.drawnHeight, 1248, 0.01),
           "single: at 260% the page is 1248 tall, got " + fx(sgl.drawnHeight, 2))
        ck(approx(sgl.panY, 696, 0.5),
           "single: zooming must hold the point under the viewport CENTRE (panY 696), not merely clamp "
           + "the old pan (which would leave 480) — got " + fx(sgl.panY, 2))
        // the held point really is back under the centre, read off the drawn item
        ck(approx(sgl.drawnY + 0.75 * sgl.drawnHeight, 240, 1.0),
           "single: the held page point must land back on the viewport centre line (240), got "
           + fx(sgl.drawnY + 0.75 * sgl.drawnHeight, 2))

        // --- zoom clamps to [100,260]; pan clamps to the page's own bounds ---
        sgl.setZoom(400)
        ck(sgl.zoomPercent === 260, "single: zoom must clamp to 260 (asked 400), got " + sgl.zoomPercent)
        sgl.setZoom(50)
        ck(sgl.zoomPercent === 100, "single: zoom must clamp to 100 (asked 50), got " + sgl.zoomPercent)
        sgl.setZoom(200)
        sgl.panBy(0, 99999)
        ck(approx(sgl.panY, sgl.panYMax, 0.01), "single: pan down must clamp to panYMax, got " + fx(sgl.panY, 2))
        sgl.panBy(0, -99999)
        ck(approx(sgl.panY, 0, 0.01), "single: pan up must clamp to 0, got " + fx(sgl.panY, 2))
        sgl.panBy(99999, 0)
        ck(approx(sgl.panX, 0, 0.01),
           "single: a page NARROWER than the frame has no horizontal headroom, so pan must stay 0, got "
           + fx(sgl.panX, 2))

        // (M7 — the hq layer's opacity across a zoom step — lives in runSinglePresented(), because it
        // is only meaningful on a surface that has REAL pixels up. This core serves provider urls that
        // never resolve offscreen, so nothing here could tell a held layer from a dimmed one.)

        sgl.setZoom(200)

        // --- a page change resets PAN, keeps ZOOM (the same law as the Pair surface: a magnified
        // volume that snapped back to 100% every turn would be unreadable) ---
        sgl.panBy(0, 200)
        ck(sgl.panY > 0, "single precondition: pan applied before the page change")
        coreSingle.pageSizes[4] = { w: 1000, h: 1500 }
        sgl.currentPage = 5
        ck(sgl.zoomPercent === 200, "single: a page turn must NOT reset zoom, got " + sgl.zoomPercent)
        ck(approx(sgl.panX, 0) && approx(sgl.panY, 0),
           "single: a page turn must RESET pan, got x=" + fx(sgl.panX, 2) + " y=" + fx(sgl.panY, 2))
        ck(coreSingle.lastVisible.length === 1 && coreSingle.lastVisible[0] === 4,
           "single: a page turn must pin exactly THAT page for decode, got " + JSON.stringify(coreSingle.lastVisible))

        // --- a failed page shows the typed placard and hides the images; a heal clears it ---
        coreSingle.pageErrors[4] = "missing_file"
        coreSingle.pageFailed(4, "missing_file")
        ck(sgl.errorCode === "missing_file",
           "single: the surface must read the BACKEND's per-page code, got '" + sgl.errorCode + "'")
        ck(sgl.errorVisible === true, "single: a failed page must show the typed placard")
        // ---- Task 11: the placard's two ways out ----
        ck(sgl.errorActionsShown === true,
           "single: a failed page's card must offer Retry/Skip — a dead end with no way out is the "
           + "defect this closes, got " + sgl.errorActionsShown)
        var sglRetried = []
        var sglSkipped = []
        sgl.retryRequested.connect(function (p) { sglRetried.push(p) })
        sgl.skipRequested.connect(function (p) { sglSkipped.push(p) })
        var sglRetry = harness.byName(sgl, "retryAction")
        var sglSkip = harness.byName(sgl, "skipAction")
        ck(sglRetry !== null && sglSkip !== null, "single: the card must carry both actions")
        if (sglRetry) sglRetry.activated()
        if (sglSkip) sglSkip.activated()
        ck(JSON.stringify(sglRetried) === "[5]",
           "single: Retry must raise the failed page, 1-based (index 4 -> 5), got " + JSON.stringify(sglRetried))
        ck(JSON.stringify(sglSkipped) === "[5]",
           "single: Skip must raise the failed page, 1-based (index 4 -> 5), got " + JSON.stringify(sglSkipped))
        // A retry takes the card down straight away (the verdict is cleared before the re-read even
        // starts), so the quiet placeholder shows instead of a stale error.
        delete coreSingle.pageErrors[4]
        coreSingle.pageRetried(4)
        ck(sgl.errorVisible === false,
           "single: pageRetried must clear the placard so the placeholder shows during the re-read")
        coreSingle.pageErrors[4] = "missing_file"
        coreSingle.pageFailed(4, "missing_file")
        ck(sgl.errorVisible === true, "single: a re-read that fails again must show the card again")

        delete coreSingle.pageErrors[4]
        coreSingle.emitPageReady(4)
        ck(sgl.errorVisible === false,
           "single: a healed page must clear the placard (MissingFile is a cooldown, not a life sentence)")
        sgl.destroy()
    }

    // presented() from the Single surface. It needs REAL pixels, so this scenario serves the file
    // fixtures (pre-warmed synchronously into the pixmap cache below) rather than provider urls that
    // resolve to nothing offscreen.
    function runSinglePresented() {
        ck(warmSinglePreview.status === Image.Ready,
           "single-presented precondition: the preview-width fixture must be warm in the pixmap cache, got "
           + warmSinglePreview.status)
        coreSinglePix.pageUrls[3] = harness.png200x300
        coreSinglePix.pageSizes[3] = { w: 200, h: 300 }
        var sgl = singleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 4, "core": coreSinglePix
        })
        if (!sgl) { failures.push("single-presented: createObject returned null"); return }
        sgl.presented.connect(function (p, f) {
            harness.singlePresentedCount += 1
            harness.singlePresentedPage = p
            harness.singlePresentedFrac = f
        })
        // The fixture is warm in the pixmap cache, so an Image resolves it in this same beat even
        // with asynchronous: true — which means the first presentation already happened before the
        // connect above could see it. Read the surface's own dedupe marker for that one.
        ck(sgl._presentedPage === 4,
           "single-presented: the surface must report the page as PRESENTED once its pixels are up, "
           + "got _presentedPage=" + sgl._presentedPage)
        // ---- THE CACHED TURN. The url and geometry for the next page are in place BEFORE the turn,
        // and both of its request sizes are pre-warmed, so this is a pure pixmap-cache hit: Image.status
        // goes straight to Ready with no dip. That is the case the previous version of this fixture did
        // NOT test — it assigned pageUrls AFTER moving currentPage, so the turn transiently pointed at
        // a provider url that fails offscreen and forced a status dip the cached case never has. The
        // assertion passed on the strength of that dip, certifying a guarantee the code did not have.
        //
        // With the ordering fixed, the ONLY thing that can notice this presentation is the explicit
        // re-check on the page-change path. Two tiers reaching Ready is still ONE presentation.
        harness.singlePresentedCount = 0
        coreSinglePix.pageUrls[4] = harness.png150x300
        coreSinglePix.pageSizes[4] = { w: 150, h: 300 }
        sgl.currentPage = 5
        ck(harness.singlePresentedCount === 0,
           "single-presented: the re-check is DEFERRED (Qt.callLater), so nothing fires synchronously "
           + "on the turn, got " + harness.singlePresentedCount)
        sgl._notePresented()      // the same entry point callLater reaches
        ck(harness.singlePresentedCount === 1,
           "single-presented: a turn onto an ALREADY-CACHED page must still report presented() exactly "
           + "once — Image.status never dips, so nothing else can notice it, got "
           + harness.singlePresentedCount)
        ck(harness.singlePresentedPage === 5,
           "single-presented: the reported page must be the one on screen (5), got " + harness.singlePresentedPage)
        ck(approx(harness.singlePresentedFrac, 0),
           "single-presented: withinPageFraction is 0 in a paged layout (a page IS the unit of "
           + "travel), got " + fx(harness.singlePresentedFrac, 3))

        // ---- A NEW BOOK, opening on the page number the last one was left on. This is the OTHER
        // hole in a marker-based presented(), and unlike the cached-turn one it depends on no cache
        // behaviour at all: the marker still says page 5, so without a reset on entryChanged the
        // first page of book B is never reported. Task 11 would read that silence as "never seen". ----
        harness.singlePresentedCount = 0
        coreSinglePix.entryChanged()          // exactly what the real core emits from openEntry
        ck(sgl._presentedPage === -1 || harness.singlePresentedCount > 0,
           "single-presented: entryChanged must clear the presented marker, got _presentedPage="
           + sgl._presentedPage)
        sgl._notePresented()                  // the same entry point callLater reaches
        ck(harness.singlePresentedCount === 1 && harness.singlePresentedPage === 5,
           "single-presented: a NEW BOOK opening on the same page number must still report "
           + "presented() — the marker is per-book, not for all time, got count="
           + harness.singlePresentedCount + " page=" + harness.singlePresentedPage)

        // ---- M7: A ZOOM STEP MUST NOT FADE THE PAGE OUT. srcCapW is a step function of the zoom, so
        // crossing 100 (and again at 180) changes sourceSize — part of an Image's cache key — and
        // re-requests the hq layer. Bound to the LIVE status, its opacity fell away to the retained
        // lower-resolution pixels and came back: two soft flashes on the way from 100% to 260%.
        //
        // The assertion reads hqTargetOpacity, the value the Behavior animates TOWARD, not the live
        // opacity — measured 2026-07-30: a synchronous read straight after a target flip still
        // returns the OLD value (1.000), so an assertion on `opacity` would pass against the flashing
        // version too and prove nothing. hqStatus is asserted alongside it so the step is known to
        // have genuinely re-requested the layer rather than been a no-op. ----
        sgl._armHqLatch()                     // the same entry point callLater reaches
        var capAt100 = sgl.srcCapW
        ck(sgl.hqStatus === Image.Ready && sgl._hqEverReady === true,
           "single M7 precondition: the hq layer must be UP and latched at zoom 100 (status="
           + sgl.hqStatus + " latched=" + sgl._hqEverReady + ")")
        ck(sgl.hqTargetOpacity === 1,
           "single M7 precondition: ...and therefore fully opaque, got " + fx(sgl.hqTargetOpacity, 2))
        sgl.setZoom(200)
        ck(sgl.srcCapW !== capAt100,
           "single M7 precondition: crossing 100 must actually change the decode cap (was " + capAt100
           + ", now " + sgl.srcCapW + ") — otherwise this assertion proves nothing")
        ck(sgl.hqStatus !== Image.Ready,
           "single M7 precondition: the new cap must actually re-request the layer (status must leave "
           + "Ready), got " + sgl.hqStatus + " — if it did not, the flash under test cannot occur here")
        ck(sgl.hqTargetOpacity === 1,
           "single M7: the hq layer must stay fully opaque across a zoom step — retainWhileLoading is "
           + "holding real pixels, so dimming them is a flash for nothing, got "
           + fx(sgl.hqTargetOpacity, 2))
        sgl.setZoom(260)
        ck(sgl.hqTargetOpacity === 1,
           "single M7: ...and across the second cap step at 180 too, got " + fx(sgl.hqTargetOpacity, 2))
        sgl.destroy()
    }

    // ==================== PHASE THREE: LET GO ====================
    // The one thing that CANNOT be proved synchronously — that the page-change path actually REACHES
    // the presented() re-check. Every other presented() assertion in this file drives _notePresented()
    // by hand (the house pattern, so no phase waits on a wall clock), which proves what the function
    // does but NOT that anything calls it: deleting the re-check from onCurrentPageChanged leaves all
    // of them green (measured — that mutation was the one survivor of nine).
    //
    // So it turns onto a page that is warm at BOTH request sizes — no status dip, therefore nothing
    // derived can notice it — and then simply lets go. The report has to arrive through the surface's
    // own Qt.callLater, or not at all.
    //
    // It takes THREE phases, not two, and the reason is a trap worth naming: the surface schedules a
    // re-check from onActiveChanged too, and mounting it in phase two leaves that one pending. It
    // fires after phase two returns — by which time currentPage has already moved — so it reports the
    // NEW page and a two-phase version passes with the page-change wiring deleted (measured: that is
    // exactly how the first draft of this phase gave a false green). So phase three DRAINS the
    // activation report first (and asserts it, which pins that path too), and only then turns.
    function setUpDeferredCheck() {
        coreSingleDefer.pageUrls[3] = harness.png200x300
        coreSingleDefer.pageSizes[3] = { w: 200, h: 300 }
        coreSingleDefer.pageUrls[4] = harness.png150x300
        coreSingleDefer.pageSizes[4] = { w: 150, h: 300 }
        deferredSingle = singleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 4, "core": coreSingleDefer
        })
        if (!deferredSingle) { failures.push("deferred: createObject returned null"); return }
        deferredSingle.presented.connect(function (p, f) {
            harness.deferredPresentedCount += 1
            harness.deferredPresentedPage = p
        })
        ck(deferredSingle.contentOnScreen === true,
           "deferred precondition: page 4's fixtures must be warm, so the surface starts with content "
           + "on screen (got " + deferredSingle.contentOnScreen + ")")
    }
    property var deferredSingle: null

    // ============================ WHAT THE SURFACES ARE DRAWING (Task 9) ============================
    // The Loupe asks each surface ONE question — "what are you painting, and where?" — and all three
    // answer in the same shape, which is what lets one lens work in Single, Pair and Long Strip
    // without a case per layout. This slice pins that contract at the source: the answer is the
    // PAPER's box (not the row's, not the viewport's), it is in the surface's own coordinates, it
    // moves when the book moves, and a surface that is not the mounted one draws nothing and says so.
    //
    // Plus the strip's WHEEL LOCK — the structural half of "the Loupe never changes the reading
    // position". The lens's own tracker swallows the wheel over the comic; this proves the column
    // cannot move even if an event reached its intake anyway.
    property var loupeStripSurface: null

    function runVisiblePageRects() {
        // ---- LONG STRIP: the rows on screen ----
        // 300-wide pages in a 520-wide viewport, so the paper is NARROWER than the row and the
        // report has to be the paper's box, not the row's. (The runStrip fixture is 800-wide pages
        // in a 520 viewport — the opposite mistake — so neither one can pass by accident.)
        stripModelLoupe.clear()
        for (var i = 0; i < 40; i++)
            stripModelLoupe.append({ pageIndex: i, top: i * 200, displayWidth: 300,
                                     displayHeight: 200, ready: true, errorCode: 0 })
        coreLoupeStrip.stripModel = stripModelLoupe
        var st = stripComp.createObject(harness, {
            "width": 520, "height": 480, "active": true, "core": coreLoupeStrip
        })
        if (!st) { failures.push("rects: strip createObject returned null"); return }
        loupeStripSurface = st
        st.forceRelayout()

        var rects = st.visiblePageRects()
        // 480 of viewport over 200-tall rows = rows 0,1,2 and the top of row 3 — but assert on the
        // GEOMETRY rather than on a count, so a change to the row height does not silently pass.
        ck(rects.length >= 2, "rects strip: a 480-tall viewport over 200-tall rows must report "
           + "several rows, got " + rects.length)
        var okStrip = true, why = ""
        for (var r = 0; r < rects.length; r++) {
            var e = rects[r]
            if (!(e.width === 300)) { okStrip = false; why = "width " + e.width + " (must be the PAPER's 300, not the row's 520)" ; break }
            if (!(Math.abs(e.x - (520 - 300) / 2) < 0.5)) { okStrip = false; why = "x " + e.x + " (the paper is centred in the row)"; break }
            if (!(e.height === 200)) { okStrip = false; why = "height " + e.height; break }
            if (String(e.url).indexOf("tier=hq") < 0) { okStrip = false; why = "url '" + e.url + "' (the lens samples the hq tier)"; break }
            if (String(e.url).indexOf("/" + e.page + "?") < 0) { okStrip = false; why = "url '" + e.url + "' is not page " + e.page + "'s"; break }
        }
        ck(okStrip, "rects strip: every reported row must be the PAPER's box on the hq tier — bad " + why)
        // the rows are the ones on screen, in order, starting at the top edge
        ck(rects.length > 0 && rects[0].page === 0 && Math.abs(rects[0].y) < 0.5,
           "rects strip: the first reported row is the one at the top edge, got page "
           + (rects.length ? rects[0].page : "<none>") + " at y " + (rects.length ? rects[0].y : "?"))
        ck(rects.length > 1 && rects[1].page === 1 && Math.abs(rects[1].y - 200) < 0.5,
           "rects strip: the next row sits one row lower, got " + JSON.stringify(rects.length > 1 ? rects[1] : null))

        // ---- ...AND IT MOVES WHEN THE BOOK MOVES. This is the whole Long Strip requirement: the
        //      page under a stationary lens changes because the COLUMN moved, never because the lens
        //      did. Park the column 3.5 rows down and the report must start on a different page. ----
        st.haltScrollAt(700)
        st.forceRelayout()
        var scrolled = st.visiblePageRects()
        ck(scrolled.length > 0 && scrolled[0].page === 3,
           "rects strip: after scrolling 700px (3.5 rows) the report must start at page 3, got "
           + (scrolled.length ? scrolled[0].page : "<none>"))
        ck(scrolled.length > 0 && Math.abs(scrolled[0].y - (-100)) < 0.5,
           "rects strip: ...with the part-scrolled row reported ABOVE the viewport top (y -100), got "
           + (scrolled.length ? scrolled[0].y : "?"))
        st.haltScrollAt(0)

        // ---- a surface that is not mounted is drawing nothing, and says so ----
        st.active = false
        ck(st.visiblePageRects().length === 0,
           "rects strip: an INACTIVE surface draws nothing and must report nothing, got "
           + st.visiblePageRects().length)
        st.active = true

        // ---- THE WHEEL LOCK ----
        // Driven through the intake function itself, not through a synthesized event: it is the ONE
        // wheel door, so a guard there cannot be routed around, and a test that pressed a real wheel
        // event would be testing Qt's delivery order instead of this rule.
        //
        // ASSERTED ON THE BACKLOG, NOT ON contentY, and that is the whole difference between a real
        // check and a vacuous one. A wheel notch does not move contentY synchronously — it queues
        // pixels into `_pendingWheelPx` and a FrameAnimation drains them over the following frames —
        // so a same-beat read of contentY is unchanged whether the lock held or not. (Measured: with
        // the guard deleted, a contentY-only assertion still passed.) The backlog IS set
        // synchronously, and manualNavigation() fires synchronously, so those two are what the rule
        // can actually be seen in.
        st.haltScrollAt(200)
        var yBefore = st.contentY
        var navBefore = harness.stripManualNavCount
        st.wheelLocked = true
        st._intakeWheel(-600, 0)
        ck(st._pendingWheelPx === 0 && st.contentY === yBefore,
           "rects strip: with the Loupe up the wheel must NOT reach the column — backlog "
           + st._pendingWheelPx + ", contentY " + st.contentY + " (was " + yBefore + ")")
        ck(harness.stripManualNavCount === navBefore,
           "rects strip: ...and a locked wheel is not a gesture either, got "
           + (harness.stripManualNavCount - navBefore) + " manualNavigation")
        st.wheelLocked = false
        st._intakeWheel(-600, 0)
        ck(st._pendingWheelPx !== 0,
           "rects strip: fixture - unlocked, the SAME wheel must queue real pixels, or the check "
           + "above proves nothing. Got backlog " + st._pendingWheelPx)
        st.haltScrollAt(0)

        // ---- SINGLE PAGE: one page, the box it is actually drawn in ----
        coreLoupeSingle.pageSizes[3] = { w: 1000, h: 1500 }
        var sg = singleComp.createObject(harness, {
            "width": 800, "height": 480, "active": true, "currentPage": 4, "core": coreLoupeSingle
        })
        if (sg) {
            var sr = sg.visiblePageRects()
            ck(sr.length === 1, "rects single: Single Page draws exactly one page, got " + sr.length)
            if (sr.length === 1) {
                ck(sr[0].page === 3, "rects single: ...page 3 (1-based 4), got " + sr[0].page)
                // Read against the surface's OWN drawn readbacks, so the two can never drift: a
                // contain-fitted 1000x1500 page in 800x480 is 320x480 at x=240.
                ck(approx(sr[0].x, sg.drawnX, 0.01) && approx(sr[0].y, sg.drawnY, 0.01)
                   && approx(sr[0].width, sg.drawnWidth, 0.01) && approx(sr[0].height, sg.drawnHeight, 0.01),
                   "rects single: the reported box must BE the drawn box, got " + JSON.stringify(sr[0])
                   + " vs drawn " + sg.drawnX + "," + sg.drawnY + " " + sg.drawnWidth + "x" + sg.drawnHeight)
                ck(approx(sr[0].width, 320, 0.01), "rects single: fixture - the drawn width is the CONTAIN fit 320, got " + sr[0].width)
                ck(String(sr[0].url).indexOf("tier=hq") >= 0,
                   "rects single: the lens samples the hq tier, got '" + sr[0].url + "'")
            }
            sg.active = false
            ck(sg.visiblePageRects().length === 0, "rects single: an INACTIVE surface reports nothing")
            sg.destroy()
        } else failures.push("rects: single createObject returned null")

        // ---- PAIR: TWO boxes, which is what lets the lens straddle the gutter ----
        coreDouble.units = { 3: { rightIndex: 3, leftIndex: 4, spread: false, coverAlone: false } }
        var dp = makeDouble({ "currentPage": 4, "rtl": true })
        if (dp) {
            var dr = dp.visiblePageRects()
            ck(dr.length === 2, "rects pair: a PAIR must report both halves — this is what lets the "
               + "lens sit across the gutter, got " + dr.length)
            if (dr.length === 2) {
                ck(dr[0].page === 3 && dr[1].page === 4,
                   "rects pair: ...the unit's two pages, got " + dr[0].page + "," + dr[1].page)
                ck(approx(dr[0].x, dp.rightIndexX, 0.01) && approx(dr[0].width, dp.rightPageWidth, 0.01)
                   && approx(dr[1].x, dp.leftIndexX, 0.01) && approx(dr[1].width, dp.leftPageWidth, 0.01),
                   "rects pair: each box must BE the drawn box, got " + JSON.stringify(dr))
                // RTL: the rightIndex page is physically to the RIGHT, so the two boxes are on
                // opposite sides of the spine and the gutter really is between them.
                ck(dr[0].x > dr[1].x, "rects pair: RTL - the two reported boxes must sit either side "
                   + "of the spine, got " + dr[0].x + " vs " + dr[1].x)
            }
            // a SINGLE/spread unit is one box, not a pair pretending
            coreDouble.units = { 7: { rightIndex: 7, leftIndex: -1, spread: true, coverAlone: false } }
            dp.currentPage = 8
            ck(dp.visiblePageRects().length === 1,
               "rects pair: a spread is ONE page, got " + dp.visiblePageRects().length)
            dp.active = false
            ck(dp.visiblePageRects().length === 0, "rects pair: an INACTIVE surface reports nothing")
            dp.destroy()
        } else failures.push("rects: double createObject returned null")
    }

    function runPhaseThree() {
        if (!deferredSingle) { report(); return }
        // DRAIN first. The mount's own presentation was noticed synchronously during construction —
        // the fixtures are warm, so contentOnScreen went true inside createObject, before the connect
        // above could see the signal — and the activation path ALSO scheduled a deferred re-check,
        // which has landed by now. Either way the marker is parked on page 4, and both are out of the
        // way before the turn below, which is the whole point of splitting these phases.
        ck(deferredSingle._presentedPage === 4,
           "deferred precondition: the mounted page must already be marked presented before the turn, "
           + "got _presentedPage=" + deferredSingle._presentedPage)

        harness.deferredPresentedCount = 0
        deferredSingle.currentPage = 5      // warm at 700 AND 1400 -> Image.status never leaves Ready
        ck(deferredSingle.contentOnScreen === true,
           "deferred precondition: THE POINT — the turn must NOT dip the status, or the derived "
           + "property would notice it on its own and this phase would prove nothing (got "
           + deferredSingle.contentOnScreen + ")")
        ck(harness.deferredPresentedCount === 0,
           "deferred: nothing may fire synchronously on the turn, got " + harness.deferredPresentedCount)
        phaseFourTimer.start()
    }
    function runPhaseFour() {
        ck(harness.deferredPresentedCount === 1,
           "deferred: the PAGE-CHANGE path must reach the presented() re-check on its own — nobody "
           + "called _notePresented() here and no status dipped, so a turn onto an already-cached "
           + "page is reported only if onCurrentPageChanged really schedules it, got "
           + harness.deferredPresentedCount)
        ck(harness.deferredPresentedPage === 5,
           "deferred: ...for the page turned TO (5), got " + harness.deferredPresentedPage)
        if (deferredSingle) deferredSingle.destroy()
        report()
    }
    Timer { id: phaseThreeTimer; interval: 40; running: false; onTriggered: harness.runPhaseThree() }
    Timer { id: phaseFourTimer;  interval: 40; running: false; onTriggered: harness.runPhaseFour() }

    Timer { id: phaseTimer; interval: 30; running: false; onTriggered: harness.runPhaseTwo() }

    function runPhaseTwo() {
        try {
            runStripFailure()
            runStripAutoScroll()       // Task 8: motion only, and it never resizes the page
            runStripRequestRange()     // Task 8: the retention window, and how often it sweeps
            runDouble()
            runDoubleMaxSeen()
            runDoubleFreshOpen()
            runDoubleUnifiedScale()
            runDoubleCentring()
            runDoubleZoomKeepsPan()
            runDoubleDecodedPair()   // synchronous: the fixtures are pre-warmed into the pixmap cache
            runDoublePairGate()      // Task 4: the pair paints as ONE unit, or not at all
            runSingle()              // Task 4: the Single Page layout
            runSinglePresented()     // Task 4: presented(), on real pixels
            runVisiblePageRects()    // Task 9: what each surface is drawing, for the Loupe
            setUpDeferredCheck()     // ...and arm the one check that has to let go (phase three)
        } catch (e) {
            failures.push("exception during phase two: " + e.message)
            report()
            return
        }
        phaseThreeTimer.start()
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
            singleComp = Qt.createComponent("../qml/comicreader/ComicReaderSingleSurface.qml")
            if (singleComp.status === Component.Error) throw new Error("single component: " + singleComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_SURFACES_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    // safety net — a true hang (not a thrown error) still fails loudly instead of stalling CI.
    // No phase waits on wall-clock any more (the decoded-pair fixtures are pre-warmed into the
    // pixmap cache and asserted synchronously), so this should never fire on any machine.
    Timer {
        interval: 15000; running: true
        onTriggered: { console.log("COMICREADER_SURFACES_FAIL: timeout"); Qt.exit(1) }
    }
}

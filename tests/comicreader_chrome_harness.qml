// Comic Reader — FAMILY GRADIENT HUD + INPUT oracle (Task 11).
//
// Instantiates qml/comicreader/ComicReaderHud.qml + ComicReaderInput.qml (and, through the HUD,
// ComicReaderIcon.qml) offscreen against an INJECTED FAKE shell (the ComicReaderShell public seam:
// currentPage/mode/rtl/max/stripFraction/zoomPercent/hasNext/hasPrev/chromeVisible/modalOpen +
// persistedMode/persistedDirection + a fake core with unitForPage) and asserts the LOGIC the two
// chrome components own — the pixel look is Hemanth's eyes-on later:
//
//   HUD (ComicReaderHud):
//     * scrub fill/knob ratio <-> page: double mode maps (page-1)/(max-1); strip mode = scroll
//       fraction; pageForRatio SNAPS to the containing canonical unit in double mode.
//     * bookmark tick marks positioned at page/(max-1).
//     * pair-aware counter string: a pair unit -> "45-46 / 230"; a single/spread -> "46 / 230".
//     * mode chip tap WRITES persistedMode (NOT mode); direction pill tap WRITES persistedDirection
//       (NOT rtl) — so a crossing's load() never resets the toggle.
//     * prev/next pills honor hasPrev/hasNext (no intent at a boundary).
//     * auto-hide: after the (pinned) interval of stillness chromeVisible flips false; toggleChrome
//       flips it back.
//     * EVERY HUD glyph is a ComicReaderIcon (its .kind is enumerable) — no Text arrow/character
//       chips (the semantic-icon-audit law; the arrow-glyph grep lives in the .ps1).
//
//   INPUT (ComicReaderInput):
//     * emits SEMANTIC actions only; keyAction(key,mods) returns the token it fired for every key in
//       the map (M mode, I direction, P nudge, O/T/B/K/L/H, Ctrl+G, F, Alt+arrows, Space/PgUp/PgDn,
//       Home/End, Ctrl+/-) and Esc obeys the order overlay -> chrome -> back.
//     * click zones over the page in DOUBLE mode navigate BY DIRECTION (RTL left=next, LTR
//       left=previous); the center third never navigates (schedules the chrome toggle).
//     * a center single click (after the 220ms disambiguation) toggles chrome; a double click
//       toggles fullscreen and cancels the pending single click.
//     * Ctrl+wheel -> zoomBy; arrows pan when zoomed and navigate otherwise.
//     * a >4px press-drag while magnified pans and CANCELS the click (panning never turns a page).
//
// HOUSE HARNESS PATTERN (mirrors comicreader_shell_harness.qml): a thrown error HANGS qml.exe
// offscreen, so `ck` never throws — it collects failures; the run prints exactly ONE
// COMICREADER_CHROME_OK when clean, else one COMICREADER_CHROME_FAIL:<msg> per failure and Qt.exit(1).
// There is a SYNC phase (all immediate assertions) then a DEFERRED phase (auto-hide timer + the
// single-click disambiguation timer, both pinned short). A safety-net Timer fails loudly on a hang.

import QtQuick

Item {
    id: harness
    width: 900; height: 600
    visible: true

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }
    function approx(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-6 : eps) }

    // find a descendant by a marker objectName (mirrors comicreader_overlays_harness.qml)
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

    // ---- fake backend core (the surfaces' unitForPage contract the HUD reads for pair/snap) ----
    component FakeCore: QtObject {
        property var units: ({})
        function unitForPage(idx0) {
            if (units[idx0] !== undefined) return units[idx0]
            return { rightIndex: idx0, leftIndex: -1, spread: false, coverAlone: false }
        }
    }

    // ---- fake shell: the ComicReaderShell public seam the HUD binds + writes ----
    component FakeShell: QtObject {
        property int currentPage: 1
        property int max: 1
        property int maxSeen: 0
        // LAYOUT + ORDER are the persisted truth (Task 3); `mode`/`rtl` are DERIVED, READONLY
        // aliases — and the fake mirrors that shape deliberately. A fake that let a test assign
        // `mode` directly could reach states the real shell (where `mode` is readonly) never can,
        // which is exactly how a fixture ends up written to the same wrong assumption as the code.
        property string layout: "long_strip"       // single_page | paired_pages | long_strip
        property string order: "ltr"               // ltr | rtl
        readonly property string mode: layout === "paired_pages" ? "double_page" : layout
        readonly property bool   rtl: order === "rtl"
        property real stripFraction: 0
        property int zoomPercent: 100
        property bool hasNext: false
        property bool hasPrev: false
        property string curLabel: ""
        property string seriesTitle: ""
        property bool chromeVisible: true
        property bool modalOpen: false
        property var bookmarkPages: []
        property var core: null
        // Task 5: the ONE overlay coordinator the chrome raises intents at. The chrome never
        // mounts or owns a surface — it only asks, and the shell decides.
        property string activeOverlay: ""          // "" | pages | image | layout | loupe
        signal backRequested()
        signal minimizeRequested()
        signal fullscreenRequested()
        signal closeRequested()
        // mirrors ComicReaderShell.openOverlay: same name re-asked = close (one temporary surface)
        property var overlaySpy: ({ calls: 0, last: "" })
        function openOverlay(name) {
            overlaySpy.calls += 1
            overlaySpy.last = String(name)
            activeOverlay = (activeOverlay === String(name)) ? "" : String(name)
        }
        function setLayout(v) { layout = String(v) }
        function setOrder(v)  { order = String(v) }
        function toggleOrder() { order = (order === "rtl") ? "ltr" : "rtl" }
        // FIX 2 spy: the scrub bubble must CONSULT this resolver rather than recompute its own
        // estimate. Fixed return (42) so the assertion proves consultation, not coincidence.
        // The counter lives INSIDE a `property var` container (mirrors this file's own sig/bump
        // pattern) rather than as its own `property int`: this function is called live from a QML
        // property binding (the bubble's `text:`), and a plain `property int` that reads-then-
        // writes ITSELF from inside that call gets captured as the binding's own dependency —
        // every increment then re-triggers the binding, which increments again: an infinite
        // "Binding loop detected" storm. Mutating a field on an already-referenced `property var`
        // never emits a change notification, so it never re-enters the binding.
        property var pageAtFractionSpy: ({ calls: 0, lastArg: -1 })
        function pageAtFraction(f) {
            pageAtFractionSpy.calls += 1
            pageAtFractionSpy.lastArg = f
            return 42
        }
    }

    FakeCore  { id: coreA }
    FakeShell { id: shellA; core: coreA }
    FakeShell { id: shellAuto }   // isolated shell for the auto-hide deferred test

    // hudAuto's host, parked away from the origin — see setupDeferred for why.
    Item { id: hudAutoHost; x: 5000; y: 5000; width: 900; height: 600 }

    property var hudComp: null
    property var inputComp: null
    property var iconComp: null
    property var hud: null
    property var hudAuto: null
    property var input: null

    // ---- input signal counters ----
    property var sig: ({})
    function bump(name) { sig[name] = (sig[name] || 0) + 1 }
    function cnt(name) { return sig[name] || 0 }
    // last panBy delta (so a scroll test can assert DIRECTION, not just that it fired)
    property real lastPanDx: 0
    property real lastPanDy: 0
    // last scrollBy screens value (so a strip-scroll test can assert MAGNITUDE, not just that it fired)
    property real lastScrollBy: 0

    function wireInput(inp) {
        inp.next.connect(function () { bump("next") })
        inp.previous.connect(function () { bump("previous") })
        inp.scrollBy.connect(function (screens) { bump("scrollBy"); harness.lastScrollBy = screens })
        inp.zoomBy.connect(function () { bump("zoomBy") })
        inp.panBy.connect(function (dx, dy) { bump("panBy"); harness.lastPanDx = dx; harness.lastPanDy = dy })
        inp.toggleChrome.connect(function () { bump("toggleChrome") })
        inp.toggleFullscreen.connect(function () { bump("toggleFullscreen") })
        inp.openSettings.connect(function () { bump("openSettings") })
        inp.openNavigator.connect(function () { bump("openNavigator") })
        inp.openThumbnails.connect(function () { bump("openThumbnails") })
        inp.toggleBookmark.connect(function () { bump("toggleBookmark") })
        inp.goToPage.connect(function () { bump("goToPage") })
        inp.closeTop.connect(function () { bump("closeTop") })
        inp.cycleMode.connect(function () { bump("cycleMode") })
        inp.nudgeCoupling.connect(function () { bump("nudgeCoupling") })
        inp.openShortcuts.connect(function () { bump("openShortcuts") })
        inp.toggleLoupe.connect(function () { bump("toggleLoupe") })
        inp.firstPage.connect(function () { bump("firstPage") })
        inp.lastPage.connect(function () { bump("lastPage") })
        inp.prevEntry.connect(function () { bump("prevEntry") })
        inp.nextEntry.connect(function () { bump("nextEntry") })
        inp.back.connect(function () { bump("back") })
    }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_CHROME_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_CHROME_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    // ============================ HUD ============================
    function runHud() {
        hud = hudComp.createObject(harness, { "reader": shellA, "width": 900, "height": 600 })
        if (!hud) { failures.push("hud: createObject returned null"); return }

        // ----- every HUD glyph is a ComicReaderIcon (its kind is enumerable) -----
        // "back" is EXEMPT: icBack is now the shared BackAction component (back-navigation
        // unification law), which owns its own vector chevron and has no glyphKind — it isn't
        // part of the per-icon audit, so it's absent from iconKinds by design.
        var kinds = hud.iconKinds
        ck(kinds !== undefined && kinds.length >= 8,
           "hud: expected >=8 ComicReaderIcon glyphs, got " + (kinds ? kinds.length : "<none>"))
        // Task 5: the retired inventory (chapters / thumbnails / settings pills) is replaced by the
        // approved six commands. prev/next survive as the progress rail's crossing arrows.
        var needed = ["prev", "next", "bookmark", "pages", "loupe", "image", "minimize", "fullscreen", "close"]
        for (var n = 0; n < needed.length; n++)
            ck(kinds && kinds.indexOf(needed[n]) >= 0, "hud: glyph '" + needed[n] + "' must be a ComicReaderIcon in the HUD, got " + JSON.stringify(kinds))

        // ----- scrub ratio<->page: PAGED maps (page-1)/(max-1) -----
        shellA.max = 230; shellA.layout = "paired_pages"; shellA.currentPage = 46; shellA.stripFraction = 0
        ck(approx(hud.fillRatio(), 45.0 / 229.0), "hud paired: fillRatio must be (46-1)/(230-1), got " + hud.fillRatio())
        // strip maps to the scroll fraction directly
        shellA.layout = "long_strip"; shellA.stripFraction = 0.37
        ck(approx(hud.fillRatio(), 0.37), "hud strip: fillRatio must equal stripFraction 0.37, got " + hud.fillRatio())
        shellA.layout = "paired_pages"

        // ----- bookmark ticks at page/(max-1) -----
        ck(approx(hud.tickRatio(45), 45.0 / 229.0), "hud: tick at page index 45 must be 45/(230-1), got " + hud.tickRatio(45))
        ck(approx(hud.tickRatio(0), 0), "hud: a tick at page 0 must be 0, got " + hud.tickRatio(0))
        ck(approx(hud.tickRatio(229), 1.0), "hud: a tick at the last page (229) must be 1.0, got " + hud.tickRatio(229))

        // ----- pageForRatio SNAPS to the containing unit in double mode -----
        // pages 44,45 (0-based) form one pair unit; a ratio landing mid-unit snaps to the unit anchor (44 -> page 45)
        coreA.units = {}
        coreA.units[44] = { rightIndex: 44, leftIndex: 45, spread: false, coverAlone: false }
        coreA.units[45] = { rightIndex: 44, leftIndex: 45, spread: false, coverAlone: false }
        var r45 = 45.0 / 229.0   // ratio that rounds to index 45 (inside the [44,45] unit)
        ck(hud.pageForRatio(r45) === 45, "hud double: pageForRatio must SNAP index 45 to its unit anchor page 45 (1-based), got " + hud.pageForRatio(r45))
        // strip mode: the scrub emits a fraction, not a snapped page — pageForRatio(0.5) is just idx+1
        shellA.layout = "long_strip"
        ck(hud.pageForRatio(0.0) === 1, "hud strip: pageForRatio(0) must be page 1, got " + hud.pageForRatio(0.0))
        shellA.layout = "paired_pages"

        // ----- pair-aware counter -----
        shellA.currentPage = 45   // anchor into the [44,45] pair unit (0-based 44)
        ck(hud.counterText() === "45–46 / 230", "hud: a pair unit counter must read '45-46 / 230', got '" + hud.counterText() + "'")
        // a single / spread reads its one page
        coreA.units = {}
        coreA.units[45] = { rightIndex: 45, leftIndex: -1, spread: false, coverAlone: false }
        shellA.currentPage = 46
        ck(hud.counterText() === "46 / 230", "hud: a single unit counter must read '46 / 230', got '" + hud.counterText() + "'")
        coreA.units[45] = { rightIndex: 45, leftIndex: -1, spread: true, coverAlone: false }
        ck(hud.counterText() === "46 / 230", "hud: a spread counter must read its one page '46 / 230', got '" + hud.counterText() + "'")

        // ----- prev/next crossing arrows honor hasPrev/hasNext -----
        shellA.hasPrev = false; shellA.hasNext = false
        sig = {}
        hud.prevRequested.connect(function () { bump("prevRequested") })
        hud.nextRequested.connect(function () { bump("nextRequested") })
        hud.pressPrev(); hud.pressNext()
        ck(cnt("prevRequested") === 0 && cnt("nextRequested") === 0, "hud: prev/next arrows at a boundary must emit NO intent, got prev=" + cnt("prevRequested") + " next=" + cnt("nextRequested"))
        ck(hud.prevEnabled === false && hud.nextEnabled === false, "hud: prev/next arrows must be disabled when hasPrev/hasNext false")
        shellA.hasPrev = true; shellA.hasNext = true
        hud.pressPrev(); hud.pressNext()
        ck(cnt("prevRequested") === 1 && cnt("nextRequested") === 1, "hud: prev/next arrows must emit their intent when enabled, got prev=" + cnt("prevRequested") + " next=" + cnt("nextRequested"))

        // ----- edge SIDE NAV BARS: every PAGED layout, direction-aware page turn -----
        // LEFT bar advances in RTL / retreats in LTR; RIGHT bar mirrors it (matches the click zones
        // and the current reader's NavBar). advance = pageNext (forward in reading), retreat = pagePrev.
        sig = {}
        hud.advancePageRequested.connect(function () { bump("advancePage") })
        hud.retreatPageRequested.connect(function () { bump("retreatPage") })
        shellA.max = 230; shellA.layout = "paired_pages"; shellA.order = "rtl"
        ck(hud.navBarsVisible === true, "hud: side nav bars must be VISIBLE in Paired Pages")
        sig = {}; hud.navBarTap(true)   // left bar, RTL -> advance (next in reading)
        ck(cnt("advancePage") === 1 && cnt("retreatPage") === 0, "hud: LEFT bar in RTL -> advance page")
        sig = {}; hud.navBarTap(false)  // right bar, RTL -> retreat
        ck(cnt("retreatPage") === 1 && cnt("advancePage") === 0, "hud: RIGHT bar in RTL -> retreat page")
        shellA.order = "ltr"
        sig = {}; hud.navBarTap(true)   // left bar, LTR -> retreat
        ck(cnt("retreatPage") === 1 && cnt("advancePage") === 0, "hud: LEFT bar in LTR -> retreat page")
        sig = {}; hud.navBarTap(false)  // right bar, LTR -> advance
        ck(cnt("advancePage") === 1 && cnt("retreatPage") === 0, "hud: RIGHT bar in LTR -> advance page")
        // DEFECT 3 (Task 5): Single Page is a PAGED layout — it turns pages exactly like a pair, so
        // the side bars must be there too. They used to be gated on `mode === "double_page"`, which
        // left Single Page with no on-screen page-turn affordance at all.
        shellA.layout = "single_page"
        ck(hud.navBarsVisible === true, "hud: side nav bars must be VISIBLE in Single Page too (paged layout)")
        // hidden in Strip mode (page turns don't apply to continuous scroll)
        shellA.layout = "long_strip"
        ck(hud.navBarsVisible === false, "hud: side nav bars must be HIDDEN in Long Strip")
        shellA.layout = "paired_pages"

        // ----- toggleChrome flips chromeVisible on the shell seam -----
        shellA.chromeVisible = true
        hud.toggleChrome()
        ck(shellA.chromeVisible === false, "hud: toggleChrome must flip the shell's chromeVisible to false")
        hud.toggleChrome()
        ck(shellA.chromeVisible === true, "hud: toggleChrome must flip chromeVisible back to true")

        // ----- FIX 3b: auto-hide is HELD while a modal is open (Task 12 CARRY) -----
        shellA.modalOpen = true; shellA.chromeVisible = true
        hud._autoHide()
        ck(shellA.chromeVisible === true, "hud: _autoHide() must leave chromeVisible TRUE while modalOpen is true")
        shellA.modalOpen = false
        hud._autoHide()
        ck(shellA.chromeVisible === false, "hud: _autoHide() must hide chrome once modalOpen is false again")
        shellA.chromeVisible = true

        // ----- FIX 2: the scrub bubble follows the POINTER (hover/drag), consulting
        // reader.pageAtFraction() (geometry-honest in Strip) instead of recomputing its own
        // estimate; the knob grows on that same interaction; the whole block hides for a
        // one-page entry. HoverHandler.hovered can't be driven from this offscreen harness (no
        // synthetic pointer injection — the file's own hover-quirk note documents that gap), so
        // this drives the DRAG arm of the shared (_scrubbing || scrubHover.hovered) condition;
        // the hover arm is the identical code path, just a different trigger.
        shellA.max = 230; shellA.layout = "long_strip"; shellA.stripFraction = 0; shellA.currentPage = 1
        var scrubTrack = byName(hud, "hudScrubTrack")
        var knob = byName(hud, "hudKnob")
        var bubbleText = byName(hud, "hudBubbleText")
        ck(scrubTrack !== null, "hud: the scrub track must be reachable (objectName 'hudScrubTrack')")
        ck(knob !== null, "hud: the scrub knob must be reachable (objectName 'hudKnob')")
        ck(bubbleText !== null, "hud: the scrub bubble text must be reachable (objectName 'hudBubbleText')")
        if (knob) harness._restKnobWidth = knob.width
        harness._knob = knob

        shellA.pageAtFractionSpy = { calls: 0, lastArg: -1 }
        hud._scrubbing = true
        hud._scrubRatio = 0.9   // far from stripFraction=0 — proves the DISPLAY follows the pointer, not the position
        ck(bubbleText === null || String(bubbleText.text).indexOf("42") >= 0,
           "hud: the scrub bubble text must show the resolver's answer (42), got '" + (bubbleText ? bubbleText.text : "<none>") + "'")
        ck(shellA.pageAtFractionSpy.calls > 0,
           "hud: the scrub bubble must CONSULT reader.pageAtFraction (not recompute its own estimate), got " + shellA.pageAtFractionSpy.calls)
        // the knob's width grows via a real `Behavior` (100ms) — qml.exe never ticks the animation
        // clock mid-script (same reason the toast fade is asserted in the DEFERRED phase below), so
        // the grow check itself is deferred; _scrubbing stays true until then.

        // ----- scrub block hides for a one-page entry -----
        var scrubBlock = byName(hud, "hudScrub")
        ck(scrubBlock !== null, "hud: the scrub block must be reachable (objectName 'hudScrub')")
        shellA.max = 230
        ck(scrubBlock === null || scrubBlock.visible === true, "hud: the scrub block must be visible for a multi-page entry")
        shellA.max = 1
        ck(scrubBlock === null || scrubBlock.visible === false, "hud: the scrub block must be HIDDEN for a one-page entry (max===1)")
        shellA.max = 230

        // ----- toast: the one transient-feedback surface (zoom, pairing, bookmarks) -----
        // The fade is a real `Behavior on opacity` (140ms) — a synchronous read right after calling
        // showToast() still observes the PRE-animation value (qml.exe never ticks the animation
        // clock mid-script), so the "presents it" assertion is deferred (see runToastDeferred);
        // only structural, non-animated facts are checked here.
        harness._toast = byName(hud, "hudToast")
        harness._toastText = byName(hud, "hudToastText")
        ck(harness._toast !== null, "toast: the HUD must mount a toast surface")
        ck(harness._toast.opacity === 0, "toast: starts hidden")
    }

    // ============ TASK 5: the approved sidebar-free command chrome ============
    // Hemanth's approved ledger, verbatim: "Thin title strip with Back and book title. One flat top
    // command bar: Bookmark, Pages, Loupe, Image, current Layout, and current Order. No reader
    // sidebar. No permanent settings drawer. One gold bottom rail with current position, total
    // pages, and scrub affordance. ... Toolbar, title toast, progress rail, and cursor sleep
    // together after 2.5 seconds of inactivity."
    //
    // He corrected an earlier mock personally on two points, and these assertions are the fence
    // around both: "colosseum does not have a side panel we can use inside the reader", and
    // "Cover's simplicity is not 'hide everything inside a modern drawer'. It is one shallow layer:
    // large, plainly named actions across the top; one unmistakable progress bar at the bottom; no
    // pill soup, no nested control architecture."
    function runCommandChrome() {
        // ---- structure: the three approved surfaces, and the one that must NOT exist ----
        ck(byName(hud, "readerTitleBar") !== null, "chrome: a thin title strip must exist (readerTitleBar)")
        ck(byName(hud, "readerCommandBar") !== null, "chrome: the flat command bar must exist (readerCommandBar)")
        ck(byName(hud, "readerProgressRail") !== null, "chrome: the gold progress rail must exist (readerProgressRail)")
        ck(byName(hud, "readerSidebar") === null, "chrome: the reader has NO sidebar")
        ck(byName(hud, "readerTitleToast") !== null, "chrome: the title toast must exist (readerTitleToast)")

        // ---- the retired pill HUD: the mode segments encoded the lossy Manga/Comic/Strip identity
        // that cannot express Single Page, which is exactly why they die rather than gain a 4th ----
        ck(byName(hud, "hudModeManga") === null && byName(hud, "hudModeComic") === null
           && byName(hud, "hudModeStrip") === null,
           "chrome: the legacy Manga/Comic/Strip mode pills must be RETIRED, not re-skinned")

        // ---- chrome sleeps after 2.5s (was 3s) ----
        ck(hud.autoHideMs === 2500, "chrome: sleeps after 2.5 seconds, got " + hud.autoHideMs)

        // ---- six direct commands, in the approved order ----
        var bar = byName(hud, "readerCommandBar")
        var want = ["bookmark", "pages", "loupe", "image", "layout", "order"]
        ck(bar !== null && bar.commands !== undefined && bar.commands.length === 6,
           "chrome: exactly SIX direct commands, got " + (bar && bar.commands ? bar.commands.length : "<none>"))
        if (bar && bar.commands) {
            for (var i = 0; i < want.length; i++)
                ck(String(bar.commands[i]) === want[i],
                   "chrome: command " + i + " must be '" + want[i] + "', got '" + bar.commands[i] + "'")
        }
        if (!bar) return

        // ---- every command raises its semantic INTENT; none of them writes core state ----
        sig = {}
        hud.openPages.connect(function () { bump("openPages") })
        hud.openLoupe.connect(function () { bump("openLoupe") })
        hud.openImage.connect(function () { bump("openImage") })
        hud.openLayout.connect(function () { bump("openLayout") })
        hud.toggleOrder.connect(function () { bump("toggleOrder") })
        hud.toggleBookmark.connect(function () { bump("toggleBookmark") })

        sig = {}; bar.trigger("bookmark"); ck(cnt("toggleBookmark") === 1, "chrome: Bookmark -> toggleBookmark, got " + cnt("toggleBookmark"))
        sig = {}; bar.trigger("pages");    ck(cnt("openPages") === 1,     "chrome: Pages -> openPages, got " + cnt("openPages"))
        sig = {}; bar.trigger("loupe");    ck(cnt("openLoupe") === 1,     "chrome: Loupe -> openLoupe, got " + cnt("openLoupe"))
        sig = {}; bar.trigger("image");    ck(cnt("openImage") === 1,     "chrome: Image -> openImage, got " + cnt("openImage"))
        sig = {}; bar.trigger("layout");   ck(cnt("openLayout") === 1,    "chrome: Layout -> openLayout, got " + cnt("openLayout"))
        sig = {}; bar.trigger("order");    ck(cnt("toggleOrder") === 1,   "chrome: Order -> toggleOrder, got " + cnt("toggleOrder"))
        // an unknown command is inert, never a silent misfire onto its neighbour
        sig = {}; bar.trigger("settings")
        ck(cnt("openPages") + cnt("openImage") + cnt("openLayout") + cnt("toggleOrder") === 0,
           "chrome: an unknown command must raise NO intent")

        // ---- the Layout and Order commands SHOW the current state (they are the readout) ----
        shellA.layout = "single_page"; shellA.order = "rtl"
        ck(bar.labelFor("layout") === "Single page", "chrome: layout label must read 'Single page', got '" + bar.labelFor("layout") + "'")
        ck(bar.glyphFor("layout") === "layoutSingle", "chrome: layout glyph must be layoutSingle, got '" + bar.glyphFor("layout") + "'")
        ck(bar.labelFor("order") === "Manga order", "chrome: RTL order label must read 'Manga order', got '" + bar.labelFor("order") + "'")
        ck(bar.glyphFor("order") === "orderRtl", "chrome: RTL order glyph must be orderRtl, got '" + bar.glyphFor("order") + "'")
        shellA.layout = "paired_pages"; shellA.order = "ltr"
        ck(bar.labelFor("layout") === "Paired pages", "chrome: layout label must read 'Paired pages', got '" + bar.labelFor("layout") + "'")
        ck(bar.glyphFor("layout") === "layoutPaired", "chrome: layout glyph must be layoutPaired, got '" + bar.glyphFor("layout") + "'")
        ck(bar.labelFor("order") === "Comic order", "chrome: LTR order label must read 'Comic order', got '" + bar.labelFor("order") + "'")
        ck(bar.glyphFor("order") === "orderLtr", "chrome: LTR order glyph must be orderLtr, got '" + bar.glyphFor("order") + "'")
        shellA.layout = "long_strip"
        ck(bar.labelFor("layout") === "Long strip", "chrome: layout label must read 'Long strip', got '" + bar.labelFor("layout") + "'")
        ck(bar.glyphFor("layout") === "layoutStrip", "chrome: layout glyph must be layoutStrip, got '" + bar.glyphFor("layout") + "'")

        // ---- GOLD IS SPARING: only the ACTIVE command wears it, and only a command whose
        // temporary surface is actually the open one is active. Order is a direct toggle with no
        // surface, so it is NEVER gold — that would say "a panel is open" when none is. ----
        shellA.activeOverlay = ""
        for (var g = 0; g < want.length; g++)
            ck(bar.activeFor(want[g]) === false,
               "chrome: with no overlay open, '" + want[g] + "' must not be gold")
        shellA.activeOverlay = "image"
        ck(bar.activeFor("image") === true, "chrome: the open Image panel makes its command gold")
        ck(bar.activeFor("pages") === false && bar.activeFor("loupe") === false
           && bar.activeFor("layout") === false && bar.activeFor("order") === false,
           "chrome: exactly ONE command may be gold at a time")
        shellA.activeOverlay = ""
        // Bookmark is gold when THIS page is bookmarked — the one non-overlay readout. It reads the
        // LIVE list (the shell mount feeds hud.bookmarkPages from reader.liveBookmarks), which is
        // the same list the rail's ticks bind to, so a toggle moves both together.
        shellA.currentPage = 46; shellA.bookmarkPages = [45]
        ck(bar.activeFor("bookmark") === true, "chrome: Bookmark reads gold on a bookmarked page")
        shellA.bookmarkPages = [7]
        ck(bar.activeFor("bookmark") === false, "chrome: Bookmark is not gold on an unbookmarked page")
        shellA.bookmarkPages = []

        // ---- DEFECT 1 (Task 5): the progress rail sat DEAD AT ZERO in Single Page. fillRatio()
        // fell back to `stripFraction`, which is always 0 in a paged layout, because its condition
        // asked `mode === "double_page"` where the real question is "is this a paged layout?". ----
        shellA.max = 230; shellA.layout = "single_page"; shellA.currentPage = 46; shellA.stripFraction = 0
        ck(approx(hud.fillRatio(), 45.0 / 229.0),
           "rail: Single Page must fill to (46-1)/(230-1), NOT sit dead at 0, got " + hud.fillRatio())

        // ---- DEFECT 2 (Task 5): dragging the rail in Single Page did nothing — _emitScrub sent the
        // STRIP command (a scroll fraction) to a layout that has no scrolling column. ----
        sig = {}
        hud.seekRequested.connect(function () { bump("seek") })
        hud.scrubFractionRequested.connect(function () { bump("scrubFraction") })
        sig = {}; hud._emitScrub(0.5)
        ck(cnt("seek") === 1 && cnt("scrubFraction") === 0,
           "rail: a Single Page scrub must SEEK a page, got seek=" + cnt("seek") + " frac=" + cnt("scrubFraction"))
        shellA.layout = "paired_pages"
        sig = {}; hud._emitScrub(0.5)
        ck(cnt("seek") === 1 && cnt("scrubFraction") === 0, "rail: a Paired Pages scrub must SEEK a page")
        shellA.layout = "long_strip"
        sig = {}; hud._emitScrub(0.5)
        ck(cnt("scrubFraction") === 1 && cnt("seek") === 0,
           "rail: a Long Strip scrub must emit a scroll FRACTION, got seek=" + cnt("seek") + " frac=" + cnt("scrubFraction"))

        // ---- the rail splits the counter the way the approved mock does: current on the left of
        // the track, total on the right. The pair-aware counter still reads as one string for
        // anything that wants it whole. ----
        shellA.layout = "paired_pages"; shellA.max = 230; shellA.currentPage = 45
        coreA.units = {}
        coreA.units[44] = { rightIndex: 44, leftIndex: 45, spread: false, coverAlone: false }
        coreA.units[45] = { rightIndex: 44, leftIndex: 45, spread: false, coverAlone: false }
        ck(hud.counterCurrentText() === "45–46", "rail: the current readout must be '45-46', got '" + hud.counterCurrentText() + "'")
        ck(hud.counterTotalText() === "230", "rail: the total readout must be '230', got '" + hud.counterTotalText() + "'")
        coreA.units = {}
        shellA.layout = "long_strip"; shellA.currentPage = 1; shellA.stripFraction = 0
    }

    // ============================ TOAST (deferred: animated opacity) ============================
    property var _toast: null
    property var _toastText: null
    property var _restKnobWidth: undefined
    property var _knob: null
    function runToastDeferred() {
        ck(harness._toast.opacity === 1, "toast: showToast presents it")
        ck(harness._toastText.text === "Zoom 160%", "toast: the message shows verbatim")
        // the toast is mounted OUTSIDE the auto-hiding chrome layer on purpose — it must still show
        // when the chrome itself is hidden, which is exactly when there is no other readout to check.
        // (opacity is already 1 here, so re-presenting at the same value settles immediately — no
        // second animation wait needed.)
        shellA.chromeVisible = false
        hud.showToast("Shifted pairing")
        // STRUCTURAL, and it has to be: QML's `opacity`/`visible` report an item's OWN values, not
        // the effective inherited ones. If the toast were re-parented inside chromeLayer — the exact
        // regression this block exists to catch — the subtree would stop being painted while these
        // two still read 1/true. The opacity check alone would pass over a broken feature; only the
        // parent check actually pins the mounting.
        ck(harness._toast.parent === hud,
           "toast: must be a DIRECT child of the hud root, never inside the auto-hiding chromeLayer")
        ck(harness._toast.opacity === 1 && harness._toast.visible === true,
           "toast: must present even when the chrome is hidden (mounted outside chromeLayer)")
        ck(harness._toastText.text === "Shifted pairing",
           "toast: message shows verbatim while chrome is hidden")
        shellA.chromeVisible = true
    }

    // ============================ INPUT (keys) ============================
    function runInputKeys() {
        input = inputComp.createObject(harness, { "width": 900, "height": 600 })
        if (!input) { failures.push("input: createObject returned null"); return }
        wireInput(input)

        function key(k, mods) { sig = {}; return input.keyAction(k, mods || Qt.NoModifier) }

        // --- simple map (mode-agnostic) ---
        ck(key(Qt.Key_M) === "cycleMode" && cnt("cycleMode") === 1, "key M -> cycleMode (Manga/Comic/Strip)")
        // I (direction toggle) is RETIRED — direction is baked into the Manga/Comic identity now.
        ck(key(Qt.Key_I) === "" && cnt("toggleDirection") === 0, "key I is unbound (RTL/LTR toggle removed)")
        ck(key(Qt.Key_P) === "nudgeCoupling" && cnt("nudgeCoupling") === 1, "key P -> nudgeCoupling (still user-nudgeable)")
        ck(key(Qt.Key_O) === "openNavigator" && cnt("openNavigator") === 1, "key O -> openNavigator")
        ck(key(Qt.Key_T) === "openThumbnails" && cnt("openThumbnails") === 1, "key T -> openThumbnails")
        ck(key(Qt.Key_B) === "toggleBookmark" && cnt("toggleBookmark") === 1, "key B -> toggleBookmark")
        ck(key(Qt.Key_K) === "openShortcuts" && cnt("openShortcuts") === 1, "key K -> openShortcuts")
        ck(key(Qt.Key_L) === "toggleLoupe" && cnt("toggleLoupe") === 1, "key L -> toggleLoupe")
        ck(key(Qt.Key_H) === "toggleChrome" && cnt("toggleChrome") === 1, "key H -> toggleChrome")
        ck(key(Qt.Key_F) === "toggleFullscreen" && cnt("toggleFullscreen") === 1, "key F -> toggleFullscreen")
        ck(key(Qt.Key_Home) === "firstPage" && cnt("firstPage") === 1, "key Home -> firstPage")
        ck(key(Qt.Key_End) === "lastPage" && cnt("lastPage") === 1, "key End -> lastPage")

        // --- Ctrl+G go-to-page; Ctrl+/- zoom ---
        ck(key(Qt.Key_G, Qt.ControlModifier) === "goToPage" && cnt("goToPage") === 1, "key Ctrl+G -> goToPage")
        ck(key(Qt.Key_Equal, Qt.ControlModifier) === "zoomBy" && cnt("zoomBy") === 1, "key Ctrl+= -> zoomBy")
        ck(key(Qt.Key_Minus, Qt.ControlModifier) === "zoomBy" && cnt("zoomBy") === 1, "key Ctrl+- -> zoomBy")

        // --- Alt+arrows cross entries ---
        ck(key(Qt.Key_Left, Qt.AltModifier) === "prevEntry" && cnt("prevEntry") === 1, "key Alt+Left -> prevEntry")
        ck(key(Qt.Key_Right, Qt.AltModifier) === "nextEntry" && cnt("nextEntry") === 1, "key Alt+Right -> nextEntry")

        // --- Space/PgDn/PgUp are mode-aware: double navigates, strip scrolls ---
        input.mode = "double_page"; input.zoomPercent = 100
        ck(key(Qt.Key_Space) === "next" && cnt("next") === 1, "key Space (double) -> next")
        ck(key(Qt.Key_PageDown) === "next" && cnt("next") === 1, "key PgDn (double) -> next")
        ck(key(Qt.Key_PageUp) === "previous" && cnt("previous") === 1, "key PgUp (double) -> previous")
        input.mode = "long_strip"
        ck(key(Qt.Key_Space) === "scrollBy" && cnt("scrollBy") === 1, "key Space (strip) -> scrollBy")
        ck(key(Qt.Key_PageUp) === "scrollBy" && cnt("scrollBy") === 1, "key PgUp (strip) -> scrollBy")

        // --- arrows navigate when unzoomed, pan when zoomed (double) ---
        input.mode = "double_page"; input.rtl = true; input.zoomPercent = 100
        ck(key(Qt.Key_Left) === "next" && cnt("next") === 1, "arrow Left (double, RTL, unzoomed) -> next")
        ck(key(Qt.Key_Right) === "previous" && cnt("previous") === 1, "arrow Right (double, RTL, unzoomed) -> previous")
        input.rtl = false
        ck(key(Qt.Key_Left) === "previous" && cnt("previous") === 1, "arrow Left (double, LTR, unzoomed) -> previous")
        ck(key(Qt.Key_Right) === "next" && cnt("next") === 1, "arrow Right (double, LTR, unzoomed) -> next")
        input.zoomPercent = 200
        ck(key(Qt.Key_Left) === "panBy" && cnt("panBy") === 1, "arrow Left (double, zoomed) -> panBy")
        ck(key(Qt.Key_Down) === "panBy" && cnt("panBy") === 1, "arrow Down (double, zoomed) -> panBy")
        input.zoomPercent = 100

        // --- Up/Down = PAN only, NEVER a flip (Tankoban Max strict model, Hemanth 2026-07-17):
        //     a too-tall spread pans within itself (surface clamps); a spread that FITS swallows it ---
        input.mode = "double_page"; input.rtl = false; input.zoomPercent = 100
        input.vScrollMax = 400                                        // spread taller than the screen
        ck(key(Qt.Key_Down) === "panBy" && cnt("panBy") === 1 && harness.lastPanDy > 0, "arrow Down (double, overflow) -> PAN down (never flip)")
        ck(key(Qt.Key_Up) === "panBy" && cnt("panBy") === 1 && harness.lastPanDy < 0, "arrow Up (double, overflow) -> PAN up (never flip)")
        ck(cnt("next") === 0 && cnt("previous") === 0, "arrow Up/Down must NEVER turn the page in double mode")
        input.vScrollMax = 0                                          // spread FITS -> swallow, no flip
        ck(key(Qt.Key_Down) === "" && cnt("panBy") === 0 && cnt("next") === 0, "arrow Down (double, fits) -> swallowed, never a flip")
        ck(key(Qt.Key_Up) === "" && cnt("panBy") === 0 && cnt("previous") === 0, "arrow Up (double, fits) -> swallowed, never a flip")

        // --- STRIP mode: Up/Down are the fine-scroll key (Reader 1: 12%, Shift 25%) — the most
        //     instinctive key in a vertical reader must not be swallowed (FIX 1) ---
        input.mode = "long_strip"; input.vScrollMax = 0
        ck(key(Qt.Key_Down) === "scrollBy" && cnt("scrollBy") === 1 && approx(harness.lastScrollBy, 0.12),
           "key Down (strip) -> scrollBy(+0.12), got " + harness.lastScrollBy)
        ck(key(Qt.Key_Up) === "scrollBy" && cnt("scrollBy") === 1 && approx(harness.lastScrollBy, -0.12),
           "key Up (strip) -> scrollBy(-0.12), got " + harness.lastScrollBy)
        ck(key(Qt.Key_Up, Qt.ShiftModifier) === "scrollBy" && cnt("scrollBy") === 1 && approx(harness.lastScrollBy, -0.25),
           "Shift+Up (strip) -> scrollBy(-0.25), got " + harness.lastScrollBy)
        ck(key(Qt.Key_Down, Qt.ShiftModifier) === "scrollBy" && cnt("scrollBy") === 1 && approx(harness.lastScrollBy, 0.25),
           "Shift+Down (strip) -> scrollBy(+0.25), got " + harness.lastScrollBy)
        // double-page ruling still holds: Up/Down in double mode never scrollBy, never flip
        input.mode = "double_page"; input.zoomPercent = 100; input.vScrollMax = 0
        sig = {}
        ck(key(Qt.Key_Down) === "" && cnt("scrollBy") === 0 && cnt("panBy") === 0 && cnt("next") === 0,
           "arrow Down (double, fits) must still NEVER scrollBy or flip")

        // --- FIX 3a: an open modal owns the keyboard — everything except Escape is gated ---
        input.mode = "double_page"; input.modalOpen = true
        sig = {}
        ck(key(Qt.Key_M) === "" && cnt("cycleMode") === 0, "key M while modalOpen -> gated (no cycleMode)")
        ck(key(Qt.Key_T) === "" && cnt("openThumbnails") === 0, "key T while modalOpen -> gated")
        ck(key(Qt.Key_Left) === "" && cnt("next") === 0 && cnt("previous") === 0, "arrow Left while modalOpen -> gated")
        input.chromeVisible = true
        ck(key(Qt.Key_Escape) === "closeTop" && cnt("closeTop") === 1, "Esc while modalOpen still fires its close action")
        input.modalOpen = false

        // --- Esc is ONE door now: closeTop, always. The LAYERING moved to the shell's coordinator,
        //     which is the only thing that knows what is actually open (Task 5). Approved contract:
        //     "Escape resolves one layer at a time: close active popover, filmstrip, or Loupe;
        //     otherwise toggle the HUD; never unexpectedly leave the book." The input used to fall
        //     through to `back` once the chrome was already hidden — pressing Escape twice threw you
        //     out of the volume. Back is the ONLY reader-to-library action. ---
        input.modalOpen = true; input.chromeVisible = true
        ck(key(Qt.Key_Escape) === "closeTop" && cnt("closeTop") === 1, "Esc (overlay up) -> closeTop")
        input.modalOpen = false; input.chromeVisible = true
        ck(key(Qt.Key_Escape) === "closeTop" && cnt("closeTop") === 1, "Esc (no overlay, chrome up) -> closeTop (the shell toggles the chrome)")
        input.modalOpen = false; input.chromeVisible = false
        ck(key(Qt.Key_Escape) === "closeTop" && cnt("closeTop") === 1 && cnt("back") === 0,
           "Esc (no overlay, chrome hidden) must NEVER exit the book, got '" + key(Qt.Key_Escape) + "'")
    }

    // ============================ INPUT (click zones + drag + dbl-click) ============================
    function runInputClicks() {
        var w = 900
        var leftX = 100, midX = 450, rightX = 800   // thirds of 900 are [0,300)(300,600)[600,900)

        // --- DOUBLE mode: left/right thirds navigate BY DIRECTION; center never navigates ---
        input.mode = "double_page"; input.zoomPercent = 100
        input.rtl = true
        sig = {}; input.pressAt(leftX, 300); input.releaseAt(leftX, w)
        ck(cnt("next") === 1 && cnt("previous") === 0, "click RTL left third -> next")
        sig = {}; input.pressAt(rightX, 300); input.releaseAt(rightX, w)
        ck(cnt("previous") === 1 && cnt("next") === 0, "click RTL right third -> previous")
        input.rtl = false
        sig = {}; input.pressAt(leftX, 300); input.releaseAt(leftX, w)
        ck(cnt("previous") === 1 && cnt("next") === 0, "click LTR left third -> previous")
        sig = {}; input.pressAt(rightX, 300); input.releaseAt(rightX, w)
        ck(cnt("next") === 1 && cnt("previous") === 0, "click LTR right third -> next")
        // center third: no navigation, schedules the chrome toggle instead
        sig = {}; input.pressAt(midX, 300); input.releaseAt(midX, w)
        ck(cnt("next") === 0 && cnt("previous") === 0, "click center third -> NO navigation")
        ck(input.singleClickRunning === true, "click center third -> schedules the single-click chrome toggle")

        // --- FIX 2: STRIP mode side-zone clicks SCROLL instead of falling through to the chrome
        //     toggle (Reader 1's third-click step, ~0.82 screens) ---
        input.mode = "long_strip"
        // the preceding center-third click left the single-click timer ARMED (as designed — the next
        // double-click test below re-arms + consumes it); a non-mid doubleClick() stops a timer with
        // no side effect (its fullscreen branch only fires for the mid zone), clearing it here too.
        input.doubleClick(leftX, w)
        sig = {}; input.pressAt(leftX, 300); input.releaseAt(leftX, w)
        ck(cnt("scrollBy") === 1 && approx(harness.lastScrollBy, -0.82), "strip left-third click -> scrollBy(-0.82), got " + harness.lastScrollBy)
        ck(input.singleClickRunning === false, "strip left-third click must NOT also schedule the chrome toggle")
        sig = {}; input.pressAt(rightX, 300); input.releaseAt(rightX, w)
        ck(cnt("scrollBy") === 1 && approx(harness.lastScrollBy, 0.82), "strip right-third click -> scrollBy(+0.82), got " + harness.lastScrollBy)
        ck(input.singleClickRunning === false, "strip right-third click must NOT also schedule the chrome toggle")
        // strip center-third click is UNCHANGED: still schedules the chrome toggle
        sig = {}; input.pressAt(midX, 300); input.releaseAt(midX, w)
        ck(cnt("scrollBy") === 0, "strip center-third click must NOT scroll")
        ck(input.singleClickRunning === true, "strip center-third click -> still schedules the single-click chrome toggle")

        // --- CENTER double-click toggles fullscreen; the trailing release must NOT re-arm chrome ---
        // NOTE: a real synthetic double-click can't be delivered to a MouseArea offscreen, so we
        // model Qt's exact delivery order: Press, Release, DblClick, TRAILING Release. The 2nd press
        // is a DblClick (pressAt does NOT run for it, so _pressed stays false through the trailing
        // release) — precisely the sequence that exposed the re-arm bug (chrome toggling ~220ms
        // after every fullscreen double-click).
        input.mode = "double_page"; input.zoomPercent = 100
        sig = {}
        input.pressAt(midX, 300); input.releaseAt(midX, w)   // Press#1, Release#1 -> arm the timer
        ck(input.singleClickRunning === true, "dbl-click precondition: single-click timer armed")
        input.doubleClick(midX, w)                           // DblClick (center) -> stop timer + fullscreen
        ck(cnt("toggleFullscreen") === 1, "center double click -> toggleFullscreen")
        ck(cnt("toggleChrome") === 0, "double click must NOT also toggle chrome (single-click cancelled)")
        ck(input.singleClickRunning === false, "double click must stop the pending single-click timer")
        input.releaseAt(midX, w)                             // TRAILING Release#2 (no matching press)
        ck(input.singleClickRunning === false, "the trailing release of a double-click must NOT re-arm the single-click timer")
        ck(cnt("toggleChrome") === 0, "the trailing release must not schedule a chrome toggle")

        // --- a SIDE-zone double-click navigates exactly ONCE and NEVER toggles fullscreen ---
        input.mode = "double_page"; input.zoomPercent = 100; input.rtl = true
        sig = {}
        input.pressAt(leftX, 300); input.releaseAt(leftX, w) // Press#1, Release#1 -> navByZone (RTL left = next)
        input.doubleClick(leftX, w)                          // DblClick on a SIDE zone -> NOT fullscreen
        input.releaseAt(leftX, w)                            // TRAILING Release#2 -> discarded (no 2nd turn)
        ck(cnt("next") === 1, "side-zone double-click must turn exactly ONE page (not two), got next=" + cnt("next"))
        ck(cnt("toggleFullscreen") === 0, "side-zone double-click must NOT toggle fullscreen")

        // --- the 220ms single-click commit toggles chrome (direct handler) ---
        input.mode = "double_page"
        sig = {}
        input.pressAt(midX, 300); input.releaseAt(midX, w)
        input._commitSingleClick()
        ck(cnt("toggleChrome") === 1, "single-click commit (timeout) -> toggleChrome")

        // --- a >4px press-drag while magnified pans and CANCELS the click (no page turn) ---
        input.mode = "double_page"; input.zoomPercent = 200; input.rtl = true
        sig = {}
        input.pressAt(leftX, 300)
        input.moveTo(leftX + 40, 300)     // 40px drift > 4px -> pans + cancels
        input.releaseAt(leftX, w)
        ck(cnt("panBy") >= 1, "magnified drag must pan (panBy fired), got " + cnt("panBy"))
        ck(cnt("next") === 0 && cnt("previous") === 0, "magnified drag must CANCEL the click (no page turn), got next=" + cnt("next") + " prev=" + cnt("previous"))
        // a sub-threshold press (<=4px) still navigates
        input.zoomPercent = 100
        sig = {}
        input.pressAt(leftX, 300); input.moveTo(leftX + 2, 300); input.releaseAt(leftX, w)
        ck(cnt("next") === 1, "a <=4px twitch must NOT cancel the click (still navigates), got next=" + cnt("next"))

        // --- Ctrl+wheel zooms (double); strip wheel is left to the strip surface ---
        input.mode = "double_page"; input.zoomPercent = 100
        sig = {}; input.wheelAction(120, 0, true)
        ck(cnt("zoomBy") === 1, "Ctrl+wheel up (double) -> zoomBy")
        input.zoomPercent = 200
        sig = {}; input.wheelAction(120, 0, false)
        ck(cnt("panBy") === 1, "wheel while magnified (double) -> panBy")
        input.mode = "long_strip"; input.zoomPercent = 100
        sig = {}; ck(input.wheelAction(120, 0, false) === "" && cnt("zoomBy") === 0 && cnt("panBy") === 0, "strip wheel is NOT consumed by the input (falls through to the strip surface)")

        // --- the wheel in DOUBLE PAGE NEVER turns the page (Tankoban Max strict model, Hemanth
        //     2026-07-17): a too-tall spread pans within itself (surface clamps); a spread that FITS
        //     swallows the wheel. Flips are keys / click zones only. ---
        input.mode = "double_page"; input.zoomPercent = 100
        input.vScrollMax = 400                                        // spread taller than the screen
        sig = {}; ck(input.wheelAction(-120, 0, false) === "panBy" && cnt("panBy") === 1 && harness.lastPanDy > 0, "wheel down (double, overflow) -> PAN down (never flip)")
        sig = {}; ck(input.wheelAction(120, 0, false) === "panBy" && cnt("panBy") === 1 && harness.lastPanDy < 0, "wheel up (double, overflow) -> PAN up (never flip)")
        sig = {}; input.wheelAction(-120, 0, false); ck(cnt("next") === 0 && cnt("previous") === 0, "wheel must NEVER turn the page in double mode")
        // a spread that FITS: the wheel is swallowed (surface clamps panBy to a no-op), still never a flip
        input.vScrollMax = 0
        sig = {}; ck(input.wheelAction(-120, 0, false) === "panBy" && cnt("next") === 0, "wheel down (double, fits) -> swallowed (panBy), never a flip")
        sig = {}; ck(input.wheelAction(120, 0, false) === "panBy" && cnt("previous") === 0, "wheel up (double, fits) -> swallowed (panBy), never a flip")
        input.vScrollMax = 0; input.zoomPercent = 100
    }

    // ============================ DEFERRED (timers) ============================
    property int _autoHideChecked: 0
    function setupDeferred() {
        // auto-hide: an isolated HUD on its own shell; reveal starts the pinned timer -> hides.
        // x offset: the offscreen QPA plants a synthetic hover at the platform's default cursor
        // point (0,0), which a second same-origin Item would receive on creation and read as
        // chromeHover.hovered=true forever (an offscreen-harness artifact, not a real bug) — parking
        // hudAuto away from that point keeps this a clean auto-hide-only test.
        hudAuto = hudComp.createObject(hudAutoHost, { "reader": shellAuto, "width": 900, "height": 600, "autoHideMs": 40 })
        if (hudAuto) { shellAuto.chromeVisible = true; hudAuto.reveal() }
        // single-click real-timer: pin short, arm a center click, expect a toggleChrome after it fires.
        input.mode = "long_strip"
        input.singleClickMs = 30
        sig = {}
        input.pressAt(450, 300); input.releaseAt(450, 900)
        // kick off the toast fade-in now so its 140ms Behavior has settled by the time deferredTimer
        // fires (220ms — comfortably past the fade, and past the 40ms/30ms auto-hide/click timers).
        hud.showToast("Zoom 160%")
        deferredTimer.start()
    }

    function runDeferred() {
        ck(hudAuto && shellAuto.chromeVisible === false, "hud: chrome must AUTO-HIDE after the (pinned) inactivity interval, chromeVisible=" + (hudAuto ? shellAuto.chromeVisible : "<null>"))
        ck(cnt("toggleChrome") === 1, "input: a lone center single click must toggle chrome after the (pinned) 220ms disambiguation, got " + cnt("toggleChrome"))
        try { runToastDeferred() } catch (e) { failures.push("exception in runToastDeferred: " + e.message) }
        // FIX 2 (deferred): the knob's `Behavior on width` (100ms) has now settled.
        ck(harness._knob === null || harness._restKnobWidth === undefined || harness._knob.width > harness._restKnobWidth,
           "hud: the knob must GROW while scrubbing (dragging), got width "
           + (harness._knob ? harness._knob.width : "<none>") + " (rest was " + harness._restKnobWidth + ")")
        if (hud) hud._scrubbing = false
        report()
    }

    Timer { id: deferredTimer; interval: 220; running: false; onTriggered: harness.runDeferred() }

    function runChecks() {
        try { runHud() } catch (e) { failures.push("exception in runHud: " + e.message) }
        try { runCommandChrome() } catch (e) { failures.push("exception in runCommandChrome: " + e.message) }
        try { runInputKeys() } catch (e) { failures.push("exception in runInputKeys: " + e.message) }
        try { runInputClicks() } catch (e) { failures.push("exception in runInputClicks: " + e.message) }
        try { setupDeferred() } catch (e) { failures.push("exception in setupDeferred: " + e.message); report() }
    }

    Component.onCompleted: {
        try {
            iconComp = Qt.createComponent("../qml/comicreader/ComicReaderIcon.qml")
            if (iconComp.status === Component.Error) throw new Error("icon component: " + iconComp.errorString())
            hudComp = Qt.createComponent("../qml/comicreader/ComicReaderHud.qml")
            if (hudComp.status === Component.Error) throw new Error("hud component: " + hudComp.errorString())
            inputComp = Qt.createComponent("../qml/comicreader/ComicReaderInput.qml")
            if (inputComp.status === Component.Error) throw new Error("input component: " + inputComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_CHROME_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    // safety net — a true hang (not a thrown error) still fails loudly instead of stalling CI
    Timer {
        interval: 8000; running: true
        onTriggered: { console.log("COMICREADER_CHROME_FAIL: timeout"); Qt.exit(1) }
    }
}

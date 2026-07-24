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
        property string mode: "long_strip"
        property bool rtl: false
        property real stripFraction: 0
        property int zoomPercent: 100
        property bool hasNext: false
        property bool hasPrev: false
        property string curLabel: ""
        property string seriesTitle: ""
        property bool chromeVisible: true
        property bool modalOpen: false
        property string persistedMode: ""
        property string persistedDirection: ""
        property var bookmarkPages: []
        property var core: null
        signal backRequested()
        signal minimizeRequested()
        signal fullscreenRequested()
        signal closeRequested()
    }

    FakeCore  { id: coreA }
    FakeShell { id: shellA; core: coreA }
    FakeShell { id: shellAuto }   // isolated shell for the auto-hide deferred test

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

    function wireInput(inp) {
        inp.next.connect(function () { bump("next") })
        inp.previous.connect(function () { bump("previous") })
        inp.scrollBy.connect(function () { bump("scrollBy") })
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
        inp.toggleDirection.connect(function () { bump("toggleDirection") })
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
        var kinds = hud.iconKinds
        ck(kinds !== undefined && kinds.length >= 9,
           "hud: expected >=9 ComicReaderIcon glyphs, got " + (kinds ? kinds.length : "<none>"))
        var needed = ["back", "prev", "next", "chapters", "thumbnails", "settings", "minimize", "fullscreen", "close"]
        for (var n = 0; n < needed.length; n++)
            ck(kinds && kinds.indexOf(needed[n]) >= 0, "hud: glyph '" + needed[n] + "' must be a ComicReaderIcon in the HUD, got " + JSON.stringify(kinds))

        // ----- scrub ratio<->page: DOUBLE maps (page-1)/(max-1) -----
        shellA.max = 230; shellA.mode = "double_page"; shellA.currentPage = 46; shellA.stripFraction = 0
        ck(approx(hud.fillRatio(), 45.0 / 229.0), "hud double: fillRatio must be (46-1)/(230-1), got " + hud.fillRatio())
        // strip maps to the scroll fraction directly
        shellA.mode = "long_strip"; shellA.stripFraction = 0.37
        ck(approx(hud.fillRatio(), 0.37), "hud strip: fillRatio must equal stripFraction 0.37, got " + hud.fillRatio())
        shellA.mode = "double_page"

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
        shellA.mode = "long_strip"
        ck(hud.pageForRatio(0.0) === 1, "hud strip: pageForRatio(0) must be page 1, got " + hud.pageForRatio(0.0))
        shellA.mode = "double_page"

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

        // ----- mode chip tap WRITES persistedMode (NOT mode) -----
        shellA.mode = "long_strip"; shellA.persistedMode = ""
        hud.setMode("double_page")
        ck(shellA.persistedMode === "double_page", "hud: mode chip tap must WRITE persistedMode='double_page', got '" + shellA.persistedMode + "'")
        ck(shellA.mode === "long_strip", "hud: mode chip tap must NOT write mode directly (still 'long_strip'), got '" + shellA.mode + "'")
        // toggleMode picks the other of the two modes off the CURRENT mode
        shellA.mode = "double_page"; shellA.persistedMode = ""
        hud.toggleMode()
        ck(shellA.persistedMode === "long_strip", "hud: toggleMode from double must persist 'long_strip', got '" + shellA.persistedMode + "'")

        // ----- direction pill tap WRITES persistedDirection (NOT rtl) -----
        shellA.rtl = true; shellA.persistedDirection = ""
        hud.toggleDirection()
        ck(shellA.persistedDirection === "ltr", "hud: direction pill tap must WRITE persistedDirection='ltr', got '" + shellA.persistedDirection + "'")
        ck(shellA.rtl === true, "hud: direction pill tap must NOT write rtl directly (still true), got " + shellA.rtl)

        // ----- prev/next pills honor hasPrev/hasNext -----
        shellA.hasPrev = false; shellA.hasNext = false
        sig = {}
        hud.prevRequested.connect(function () { bump("prevRequested") })
        hud.nextRequested.connect(function () { bump("nextRequested") })
        hud.pressPrev(); hud.pressNext()
        ck(cnt("prevRequested") === 0 && cnt("nextRequested") === 0, "hud: prev/next pills at a boundary must emit NO intent, got prev=" + cnt("prevRequested") + " next=" + cnt("nextRequested"))
        ck(hud.prevEnabled === false && hud.nextEnabled === false, "hud: prev/next pills must be disabled when hasPrev/hasNext false")
        shellA.hasPrev = true; shellA.hasNext = true
        hud.pressPrev(); hud.pressNext()
        ck(cnt("prevRequested") === 1 && cnt("nextRequested") === 1, "hud: prev/next pills must emit their intent when enabled, got prev=" + cnt("prevRequested") + " next=" + cnt("nextRequested"))

        // ----- toggleChrome flips chromeVisible on the shell seam -----
        shellA.chromeVisible = true
        hud.toggleChrome()
        ck(shellA.chromeVisible === false, "hud: toggleChrome must flip the shell's chromeVisible to false")
        hud.toggleChrome()
        ck(shellA.chromeVisible === true, "hud: toggleChrome must flip chromeVisible back to true")
    }

    // ============================ INPUT (keys) ============================
    function runInputKeys() {
        input = inputComp.createObject(harness, { "width": 900, "height": 600 })
        if (!input) { failures.push("input: createObject returned null"); return }
        wireInput(input)

        function key(k, mods) { sig = {}; return input.keyAction(k, mods || Qt.NoModifier) }

        // --- simple map (mode-agnostic) ---
        ck(key(Qt.Key_M) === "cycleMode" && cnt("cycleMode") === 1, "key M -> cycleMode")
        ck(key(Qt.Key_I) === "toggleDirection" && cnt("toggleDirection") === 1, "key I -> toggleDirection")
        ck(key(Qt.Key_P) === "nudgeCoupling" && cnt("nudgeCoupling") === 1, "key P -> nudgeCoupling")
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

        // --- Up/Down VERTICAL SCROLL at base zoom when fill-width content overflows the viewport
        //     (fill-width + scroll model): scroll the overflow, turn the page only at the top/bottom edge ---
        input.mode = "double_page"; input.rtl = false; input.zoomPercent = 100
        input.vScrollMax = 400; input.vScrollPos = 0                  // overflow, at TOP
        ck(key(Qt.Key_Down) === "panBy" && cnt("panBy") === 1, "arrow Down (base zoom, overflow, not at bottom) -> scroll (panBy)")
        ck(harness.lastPanDy > 0, "arrow Down scroll must move panY DOWN (+dy reveals the bottom), got dy=" + harness.lastPanDy)
        input.vScrollPos = 400                                        // at BOTTOM edge
        ck(key(Qt.Key_Down) === "next" && cnt("next") === 1, "arrow Down (base zoom, AT bottom edge) -> next page")
        input.vScrollPos = 400                                        // scroll up from bottom
        ck(key(Qt.Key_Up) === "panBy" && cnt("panBy") === 1, "arrow Up (base zoom, overflow, not at top) -> scroll (panBy)")
        ck(harness.lastPanDy < 0, "arrow Up scroll must move panY UP (-dy reveals the top), got dy=" + harness.lastPanDy)
        input.vScrollPos = 0                                          // at TOP edge
        ck(key(Qt.Key_Up) === "previous" && cnt("previous") === 1, "arrow Up (base zoom, AT top edge) -> previous page")
        // no overflow: Up/Down navigate as before
        input.vScrollMax = 0; input.vScrollPos = 0
        ck(key(Qt.Key_Down) === "next" && cnt("next") === 1, "arrow Down (base zoom, NO overflow) -> next")
        ck(key(Qt.Key_Up) === "previous" && cnt("previous") === 1, "arrow Up (base zoom, NO overflow) -> previous")
        input.vScrollMax = 0; input.vScrollPos = 0

        // --- Esc order: overlay -> chrome -> back ---
        input.modalOpen = true; input.chromeVisible = true
        ck(key(Qt.Key_Escape) === "closeTop" && cnt("closeTop") === 1, "Esc (overlay up) -> closeTop")
        input.modalOpen = false; input.chromeVisible = true
        ck(key(Qt.Key_Escape) === "toggleChrome" && cnt("toggleChrome") === 1, "Esc (no overlay, chrome up) -> hide chrome (toggleChrome)")
        input.modalOpen = false; input.chromeVisible = false
        ck(key(Qt.Key_Escape) === "back" && cnt("back") === 1, "Esc (no overlay, chrome hidden) -> back")
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

        // --- base-zoom VERTICAL SCROLL on the wheel: fill-width content taller than the viewport
        //     scrolls; the page turns only at the top/bottom edge (fill-width + scroll model) ---
        input.mode = "double_page"; input.zoomPercent = 100
        input.vScrollMax = 400; input.vScrollPos = 0                  // overflow, at TOP
        sig = {}; ck(input.wheelAction(-120, 0, false) === "panBy" && cnt("panBy") === 1, "wheel down (base zoom, overflow, at top) -> scroll (panBy)")
        ck(harness.lastPanDy > 0, "wheel-down scroll must move panY DOWN (+dy reveals bottom), got dy=" + harness.lastPanDy)
        input.vScrollPos = 400                                        // at BOTTOM edge
        sig = {}; ck(input.wheelAction(-120, 0, false) === "next" && cnt("next") === 1, "wheel down (base zoom, AT bottom edge) -> next page")
        input.vScrollPos = 400                                        // scroll up from bottom
        sig = {}; ck(input.wheelAction(120, 0, false) === "panBy" && cnt("panBy") === 1, "wheel up (base zoom, overflow, at bottom) -> scroll (panBy)")
        ck(harness.lastPanDy < 0, "wheel-up scroll must move panY UP (-dy reveals top), got dy=" + harness.lastPanDy)
        input.vScrollPos = 0                                          // at TOP edge
        sig = {}; ck(input.wheelAction(120, 0, false) === "previous" && cnt("previous") === 1, "wheel up (base zoom, AT top edge) -> previous page")
        // no overflow: the wheel turns the page
        input.vScrollMax = 0; input.vScrollPos = 0
        sig = {}; ck(input.wheelAction(-120, 0, false) === "next" && cnt("next") === 1, "wheel down (base zoom, NO overflow) -> next")
        sig = {}; ck(input.wheelAction(120, 0, false) === "previous" && cnt("previous") === 1, "wheel up (base zoom, NO overflow) -> previous")
        input.vScrollMax = 0; input.vScrollPos = 0; input.zoomPercent = 100
    }

    // ============================ DEFERRED (timers) ============================
    property int _autoHideChecked: 0
    function setupDeferred() {
        // auto-hide: an isolated HUD on its own shell; reveal starts the pinned timer -> hides.
        hudAuto = hudComp.createObject(harness, { "reader": shellAuto, "width": 900, "height": 600, "autoHideMs": 40 })
        if (hudAuto) { shellAuto.chromeVisible = true; hudAuto.reveal() }
        // single-click real-timer: pin short, arm a center click, expect a toggleChrome after it fires.
        input.mode = "long_strip"
        input.singleClickMs = 30
        sig = {}
        input.pressAt(450, 300); input.releaseAt(450, 900)
        deferredTimer.start()
    }

    function runDeferred() {
        ck(hudAuto && shellAuto.chromeVisible === false, "hud: chrome must AUTO-HIDE after the (pinned) inactivity interval, chromeVisible=" + (hudAuto ? shellAuto.chromeVisible : "<null>"))
        ck(cnt("toggleChrome") === 1, "input: a lone center single click must toggle chrome after the (pinned) 220ms disambiguation, got " + cnt("toggleChrome"))
        report()
    }

    Timer { id: deferredTimer; interval: 160; running: false; onTriggered: harness.runDeferred() }

    function runChecks() {
        try { runHud() } catch (e) { failures.push("exception in runHud: " + e.message) }
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

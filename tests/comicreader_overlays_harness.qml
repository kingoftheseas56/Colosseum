// Comic Reader — OVERLAYS oracle (Task 12). Instantiates the overlay surfaces offscreen with an
// INJECTED FAKE reader seam (the ComicReaderShell-facing API the overlays read/write) and asserts
// their behaviour. The pixel look is Hemanth's eyes-on; this pins BEHAVIOUR.
//
// SLICE 1 — ComicReaderSettingsSheet, DISPLAY section (mode + direction on existing persisted seams):
//   * closed by default; open() shows it and reports `opened`; a fake reader's modalOpen tracks it.
//   * DISPLAY: Mode chips reflect reader.mode (the active chip is `on`); tapping the inactive chip
//     writes reader.persistedMode (NOT reader.mode — a crossing's load() owns mode). Direction chips
//     reflect reader.rtl and write reader.persistedDirection. gold marks ONLY the active choice.
//   * dismiss: the X, a scrim tap, and close() all fire dismissed() and clear `opened`.
//   * click-swallower: a tap on the sheet BODY does not fall through to the scrim (no dismiss).
//
// SLICE 2 — ComicReaderPagesOverlay, the temporary Pages filmstrip (Task 6):
//   * VIRTUALIZED: a 1,452-page volume instantiates a handful of thumbnails, not 1,452, and a
//     CLOSED filmstrip instantiates none at all.
//   * the current page is CENTERED (real delegate geometry against the real viewport centre, not
//     the component's own arithmetic) and is the largest thumbnail in the strip.
//   * RTL mirrors the VISUAL sequence only — asserted twice: through visualPageAt(), and through
//     the laid-out delegates' actual x positions — while the printed page numbers stay truthful.
//   * jump: exactly ONE jumpRequested(page) and exactly ONE dismissRequested(), from the delegate's
//     own door as well as the public one; an out-of-range index emits neither.
//   * dismiss (the canvas catcher, and dismiss()) emits dismissRequested and NEVER a jump, and
//     leaves currentPage untouched.
//   * thumbnails are requested on the `thumbnail` tier — never `hq` (Task 2's cap is the whole
//     reason a filmstrip can exist without pulling full-resolution scans).
//   * bookmarks mark the filmstrip thumbnails (the rail's other half — the progress ticks — is
//     pinned by tests/comicreader_chrome_harness.qml's tickRatio checks, and the shell gate proves
//     ONE list feeds both).
//
// SLICE 3 — ComicReaderImagePopover, the compact Image panel (Task 7):
//   * three controls visible immediately (Quality, Brightness, Night filter) and four more behind
//     ONE "Advanced image tools" disclosure that GROWS THE SAME PANEL — never a second surface.
//   * every control emits a COMPLETE profile map: the backend REPLACES rather than merges, so a
//     partial map would silently un-rotate the book.
//   * clamping at the panel's own door (gamma never 0, rotation snaps to a quarter turn) and a
//     partial/garbage profile reading back as the DEFAULTS, never NaN.
//   * the handle follows the live profile — except while it is held, so the shell's apply throttle
//     cannot yank a drag backwards.
//   * dismiss (catcher and dismiss()) changes NOTHING about the picture; the panel swallows its
//     own clicks.
//
// HOUSE HARNESS PATTERN (mirrors comicreader_surfaces_harness.qml): a thrown error hangs qml.exe
// offscreen, so `ck` never throws — it collects failures; prints exactly one COMICREADER_OVERLAYS_OK
// when clean, else one COMICREADER_OVERLAYS_FAIL:<msg> per failure and Qt.exit(1).

import QtQuick

Item {
    id: harness
    width: 1000; height: 700
    visible: true

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    // ---- fake shell/reader seam: the ComicReaderShell-facing API the settings sheet reads/writes ----
    component FakeReader: QtObject {
        // the single user-facing identity (Hemanth 2026-07-25): manga | comic | strip.
        property string readingMode: "manga"
        property string persistedMode: ""         // setReadingMode writes the internal layout seam
        property string persistedDirection: ""    // setReadingMode writes the internal direction seam
        property string nightVeil: "off"          // "off" | "low" | "high" — the sheet writes HERE (live setting)
        property real   gutterStrength: 0.35      // double-page gutter shadow (0/.22/.35/.55) — the sheet writes HERE
        property int    zoomPercent: 100          // double-page zoom (readout only this pass)
        property bool   modalOpen: false
        // coupling: the sheet's Coupling row reads the MODE (auto|manual) and drives the two
        // halves — Nudge pins the phase by hand, Auto hands it back to the probe.
        property string couplingMode: "auto"
        property int    nudgeCount: 0
        property int    resetCount: 0
        function nudgeCoupling() { nudgeCount += 1; couplingMode = "manual" }
        function resetCoupling() { resetCount += 1; couplingMode = "auto" }
        // long strip taste — portrait page width % + inter-page gap px (one setter, both values)
        property int stripWidthPct: 78
        property int stripGap: 0
        function setStripLayout(w, g) { stripWidthPct = w; stripGap = g }
        // tool grid: the sheet invokes the shell's request signals; the fake counts them as plain
        // functions (invoking a QML signal and calling a function look identical at the call site).
        property int loupeCount: 0
        property int bookmarksCount: 0
        property int thumbsCount: 0
        property int shortcutsCount: 0
        function loupeRequested()      { loupeCount += 1 }
        function bookmarksRequested()  { bookmarksCount += 1 }
        function thumbnailsRequested() { thumbsCount += 1 }
        function shortcutsRequested()  { shortcutsCount += 1 }
        // memory saver (cache budget 512 -> 256 MiB)
        property bool memorySaver: false
        function setMemorySaver(on) { memorySaver = on }
        // danger actions — both destructive, so the sheet must ARM before it fires either
        property int clearResumeCount: 0
        property int resetSeriesCount: 0
        function clearResume() { clearResumeCount += 1 }
        function resetSeries() { resetSeriesCount += 1 }
        // mirror the real shell: translate the identity to the internal layout+direction seams
        function setReadingMode(rm) {
            readingMode = rm
            persistedMode = (rm === "strip") ? "long_strip" : "double_page"
            persistedDirection = (rm === "manga") ? "rtl" : "ltr"
        }
    }

    FakeReader { id: fakeReader }

    property var sheetComp: null
    property var sheet: null

    // find a descendant by a marker objectName (the sheet tags its interactive bits)
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
    function clickCenter(item) {
        if (!item) return false
        // synthesize a click by invoking the item's tapped() if present, else via a MouseArea child.
        if (typeof item.tapped === "function") { item.tapped(); return true }
        return false
    }

    function report() {
        if (failures.length === 0) { console.log("COMICREADER_OVERLAYS_OK"); Qt.exit(0) }
        else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_OVERLAYS_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function runSettings() {
        sheet = sheetComp.createObject(harness, { "anchors.fill": harness, "reader": fakeReader })
        if (!sheet) { failures.push("settings: createObject returned null"); return }

        // --- closed by default ---
        ck(sheet.opened === false, "settings: must start CLOSED")
        ck(fakeReader.modalOpen === false, "settings: modalOpen must be false while closed")

        // --- open ---
        sheet.open()
        ck(sheet.opened === true, "settings: open() must set opened=true")

        // --- DISPLAY: ONE Mode row — Manga / Comic / Strip (direction baked in). Chips reflect
        //     reader.readingMode; a tap calls reader.setReadingMode(rm) which writes the internal
        //     layout + direction seams (no separate RTL/LTR toggle). ---
        var modeManga = byName(sheet, "settingsModeManga")
        var modeComic = byName(sheet, "settingsModeComic")
        var modeStrip = byName(sheet, "settingsModeStrip")
        ck(modeManga !== null && modeComic !== null && modeStrip !== null, "settings: Mode chips (Manga/Comic/Strip) must exist")
        ck(modeManga && modeManga.active === true, "settings: Manga chip ACTIVE when readingMode=manga")
        ck(modeComic && modeComic.active === false && modeStrip && modeStrip.active === false, "settings: only Manga active at default")
        clickCenter(modeComic)
        ck(fakeReader.readingMode === "comic", "settings: tapping Comic must set readingMode=comic, got '" + fakeReader.readingMode + "'")
        ck(fakeReader.persistedMode === "double_page" && fakeReader.persistedDirection === "ltr",
           "settings: Comic must write double_page + LTR seams, got '" + fakeReader.persistedMode + "'/'" + fakeReader.persistedDirection + "'")
        clickCenter(modeStrip)
        ck(fakeReader.readingMode === "strip", "settings: tapping Strip must set readingMode=strip")
        ck(fakeReader.persistedMode === "long_strip", "settings: Strip must write long_strip layout, got '" + fakeReader.persistedMode + "'")
        // reflect a mode change coming back from the shell
        fakeReader.readingMode = "manga"
        ck(modeManga.active === true && modeComic.active === false && modeStrip.active === false,
           "settings: Mode chips must re-reflect when readingMode changes back to manga")

        // --- DISPLAY: Night veil chips reflect reader.nightVeil, tap writes it live (Off default) ---
        var veilOff  = byName(sheet, "settingsVeilOff")
        var veilLow  = byName(sheet, "settingsVeilLow")
        var veilHigh = byName(sheet, "settingsVeilHigh")
        ck(veilOff !== null && veilLow !== null && veilHigh !== null, "settings: Night veil chips (Off/Low/High) must exist")
        ck(veilOff && veilOff.active === true, "settings: Off chip must be ACTIVE when reader.nightVeil=off (default)")
        ck(veilLow && veilLow.active === false && veilHigh && veilHigh.active === false, "settings: only Off active by default")
        clickCenter(veilLow)
        ck(fakeReader.nightVeil === "low", "settings: tapping Low must write reader.nightVeil=low, got '" + fakeReader.nightVeil + "'")
        // reflect a level change coming back from the shell
        fakeReader.nightVeil = "high"
        ck(veilHigh.active === true && veilOff.active === false && veilLow.active === false,
           "settings: Night veil chips must re-reflect when reader.nightVeil changes (High active)")
        fakeReader.nightVeil = "off"   // restore for later assertions

        // --- DOUBLE PAGE section: mode-aware, gutter presets (0/.22/.35/.55), zoom readout ---
        fakeReader.readingMode = "manga"   // the double section shows in Manga/Comic (double-page), not Strip
        var dpSection = byName(sheet, "settingsDoubleSection")
        ck(dpSection !== null, "settings: DOUBLE PAGE section must exist")
        ck(dpSection && dpSection.visible === true, "settings: DOUBLE PAGE section must be VISIBLE in double_page mode")
        // gutter shadow chips reflect reader.gutterStrength (Medium=0.35 default) and write the presets
        var gOff = byName(sheet, "settingsGutterOff"),    gSub = byName(sheet, "settingsGutterSubtle")
        var gMed = byName(sheet, "settingsGutterMedium"), gStr = byName(sheet, "settingsGutterStrong")
        ck(gOff && gSub && gMed && gStr, "settings: Gutter chips (Off/Subtle/Medium/Strong) must exist")
        ck(gMed && gMed.active === true, "settings: Medium gutter chip must be ACTIVE at the 0.35 default")
        clickCenter(gStr)
        ck(Math.abs(fakeReader.gutterStrength - 0.55) < 1e-9, "settings: tapping Strong must write gutterStrength=0.55, got " + fakeReader.gutterStrength)
        clickCenter(gOff)
        ck(fakeReader.gutterStrength === 0, "settings: tapping Off must write gutterStrength=0, got " + fakeReader.gutterStrength)
        fakeReader.gutterStrength = 0.22   // reflect a change coming back
        ck(gSub.active === true && gOff.active === false, "settings: gutter chips must re-reflect when gutterStrength changes (Subtle active)")
        // zoom readout reflects reader.zoomPercent (display only this pass)
        var zoomVal = byName(sheet, "settingsZoomValue")
        ck(zoomVal !== null, "settings: Zoom readout must exist")
        ck(zoomVal && String(zoomVal.text).indexOf("100") >= 0, "settings: Zoom readout must show 100(%), got '" + (zoomVal ? zoomVal.text : "") + "'")
        fakeReader.zoomPercent = 180
        ck(zoomVal && String(zoomVal.text).indexOf("180") >= 0, "settings: Zoom readout must reflect a zoomPercent change to 180, got '" + (zoomVal ? zoomVal.text : "") + "'")
        // --- DOUBLE PAGE: Coupling row — Auto | Nudge (mock surface 02) ---
        // Nudge pins the phase by hand; Auto hands the decision back to the probe. The row reads
        // reader.couplingMode, so the active chip tells you WHO owns the phase right now.
        var cAuto  = byName(sheet, "settingsCouplingAuto")
        var cNudge = byName(sheet, "settingsCouplingNudge")
        ck(cAuto !== null && cNudge !== null, "settings: Coupling chips (Auto/Nudge) must exist")
        ck(cAuto && cAuto.active === true, "settings: Auto chip must be ACTIVE while couplingMode=auto")
        ck(cNudge && cNudge.active === false, "settings: Nudge chip must be INACTIVE while coupling is auto")
        // tapping Auto while ALREADY auto must not fire a redundant re-probe (the probe decodes
        // pages at low priority — a no-op tap should stay a no-op).
        clickCenter(cAuto)
        ck(fakeReader.resetCount === 0, "settings: tapping Auto while already auto must NOT re-probe")
        // Nudge -> manual
        clickCenter(cNudge)
        ck(fakeReader.nudgeCount === 1, "settings: tapping Nudge must call reader.nudgeCoupling(), got " + fakeReader.nudgeCount)
        ck(fakeReader.couplingMode === "manual", "settings: Nudge must leave coupling MANUAL")
        ck(cNudge.active === true && cAuto.active === false,
           "settings: with coupling manual, Nudge is the active chip and Auto is not")
        // a second nudge is a fresh hand-flip, not a no-op (it flips the phase again)
        clickCenter(cNudge)
        ck(fakeReader.nudgeCount === 2, "settings: a second Nudge must flip again, got " + fakeReader.nudgeCount)
        // Auto -> hand it back to the probe
        clickCenter(cAuto)
        ck(fakeReader.resetCount === 1, "settings: tapping Auto while MANUAL must call reader.resetCoupling(), got " + fakeReader.resetCount)
        ck(fakeReader.couplingMode === "auto", "settings: reset must return coupling to auto")
        ck(cAuto.active === true, "settings: Auto chip re-reflects as active after the reset")

        // mode-aware: in Strip the whole double section yields (hidden)
        fakeReader.readingMode = "strip"
        ck(dpSection.visible === false, "settings: DOUBLE PAGE section must be HIDDEN in Strip mode")

        // --- LONG STRIP section: the mirror of DOUBLE PAGE — shows ONLY in Strip ---
        var stripSection = byName(sheet, "settingsStripSection")
        ck(stripSection !== null, "settings: LONG STRIP section must exist")
        ck(stripSection && stripSection.visible === true, "settings: LONG STRIP section must be VISIBLE in Strip mode")

        // page width presets 62/78/90/100 — the sheet reads reader.stripWidthPct
        var wNarrow = byName(sheet, "settingsStripWidthNarrow"),  wComfort = byName(sheet, "settingsStripWidthComfort")
        var wWide   = byName(sheet, "settingsStripWidthWide"),    wFull    = byName(sheet, "settingsStripWidthFull")
        ck(wNarrow && wComfort && wWide && wFull, "settings: Page width chips (Narrow/Comfort/Wide/Full) must exist")
        ck(wComfort && wComfort.active === true, "settings: Comfort chip must be ACTIVE at the 78% default")
        clickCenter(wWide)
        ck(fakeReader.stripWidthPct === 90, "settings: tapping Wide must write stripWidthPct=90, got " + fakeReader.stripWidthPct)
        ck(fakeReader.stripGap === 0, "settings: changing width must PRESERVE the gap (one setter, both values)")

        // gap presets 0/8/20
        var gapNone = byName(sheet, "settingsStripGapNone"), gapThin = byName(sheet, "settingsStripGapThin")
        var gapWide = byName(sheet, "settingsStripGapWide")
        ck(gapNone && gapThin && gapWide, "settings: Gap chips (None/Thin/Wide) must exist")
        ck(gapNone && gapNone.active === true, "settings: None gap chip must be ACTIVE at the 0 default")
        clickCenter(gapThin)
        ck(fakeReader.stripGap === 8, "settings: tapping Thin must write stripGap=8, got " + fakeReader.stripGap)
        ck(fakeReader.stripWidthPct === 90, "settings: changing gap must PRESERVE the width, got " + fakeReader.stripWidthPct)
        // re-reflect values arriving from the shell
        fakeReader.stripWidthPct = 62
        fakeReader.stripGap = 20
        ck(wNarrow.active === true && wWide.active === false, "settings: width chips must re-reflect a stripWidthPct change (Narrow active)")
        ck(gapWide.active === true && gapThin.active === false, "settings: gap chips must re-reflect a stripGap change (Wide active)")

        fakeReader.readingMode = "manga"   // restore
        ck(stripSection.visible === false, "settings: LONG STRIP section must be HIDDEN outside Strip mode")

        // --- TOOLS: a 2x2 launcher grid, mode-independent (shows in every mode) ---
        var tLoupe  = byName(sheet, "settingsToolLoupe"),      tBooks = byName(sheet, "settingsToolBookmarks")
        var tThumbs = byName(sheet, "settingsToolThumbnails"), tKeys  = byName(sheet, "settingsToolShortcuts")
        ck(tLoupe && tBooks && tThumbs && tKeys, "settings: all four tool tiles must exist")
        clickCenter(tLoupe);  ck(fakeReader.loupeCount === 1,     "settings: Loupe tile must request the loupe")
        clickCenter(tBooks);  ck(fakeReader.bookmarksCount === 1, "settings: Bookmarks tile must request the bookmarks overlay")
        clickCenter(tThumbs); ck(fakeReader.thumbsCount === 1,    "settings: Thumbnails tile must request thumbnails")
        clickCenter(tKeys);   ck(fakeReader.shortcutsCount === 1, "settings: Shortcuts tile must request the shortcuts sheet")
        // every tile is an icon tile — no Text-glyph chips (semantic-icon-audit law)
        ck(byName(tLoupe, "settingsToolLoupeIcon") !== null, "settings: the Loupe tile must carry a real ComicReaderIcon, not a text glyph")
        // the grid is NOT mode-gated — the tools apply in Strip too
        fakeReader.readingMode = "strip"
        ck(tLoupe.visible === true, "settings: the TOOLS grid must stay visible in Strip mode")
        fakeReader.readingMode = "manga"

        // --- Memory saver: a switch, reflecting reader.memorySaver, writing setMemorySaver ---
        var mem = byName(sheet, "settingsMemorySaver")
        ck(mem !== null, "settings: Memory saver switch must exist")
        ck(mem && mem.checked === false, "settings: Memory saver must be OFF by default")
        clickCenter(mem)
        ck(fakeReader.memorySaver === true, "settings: tapping the switch must call setMemorySaver(true)")
        ck(mem.checked === true, "settings: the switch must read back as ON")
        clickCenter(mem)
        ck(fakeReader.memorySaver === false, "settings: tapping again must call setMemorySaver(false)")
        // reflect a change arriving from the shell
        fakeReader.memorySaver = true
        ck(mem.checked === true, "settings: the switch must re-reflect a memorySaver change from the shell")
        fakeReader.memorySaver = false

        // --- Danger row: both actions destroy state, so a single tap must ARM, never fire ---
        var dClear = byName(sheet, "settingsDangerClearResume")
        var dReset = byName(sheet, "settingsDangerResetSeries")
        ck(dClear && dReset, "settings: both danger actions must exist")
        ck(dClear && dClear.armed === false && dReset && dReset.armed === false,
           "settings: danger actions must start UNARMED")
        clickCenter(dClear)
        ck(fakeReader.clearResumeCount === 0, "settings: the FIRST tap on Clear resume must NOT fire it")
        ck(dClear.armed === true, "settings: the first tap must ARM Clear resume")
        clickCenter(dClear)
        ck(fakeReader.clearResumeCount === 1, "settings: the SECOND tap must fire clearResume(), got " + fakeReader.clearResumeCount)
        ck(dClear.armed === false, "settings: firing must disarm again")

        // arming one action disarms the other — two armed hammers is how you hit the wrong one
        clickCenter(dReset)
        ck(dReset.armed === true, "settings: Reset series arms on its first tap")
        clickCenter(dClear)
        ck(dReset.armed === false, "settings: arming Clear resume must DISARM Reset series")
        ck(dClear.armed === true && fakeReader.clearResumeCount === 1, "settings: Clear resume is the armed one now, and did not fire")
        clickCenter(dClear)
        ck(fakeReader.clearResumeCount === 2, "settings: the armed action fires on its second tap")
        clickCenter(dReset); clickCenter(dReset)
        ck(fakeReader.resetSeriesCount === 1, "settings: Reset series fires on its own second tap, got " + fakeReader.resetSeriesCount)

        // closing the sheet must leave nothing armed behind for the next open
        clickCenter(dReset)
        ck(dReset.armed === true, "settings: re-arm before the close test")
        sheet.close(); sheet.open()
        ck(dReset.armed === false, "settings: closing the sheet must DISARM every danger action")

        // --- dismiss: X ---
        harness.dismissCount = 0
        sheet.dismissed.connect(function () { harness.dismissCount += 1 })
        var xBtn = byName(sheet, "settingsCloseX")
        ck(xBtn !== null, "settings: close X must exist")
        clickCenter(xBtn)
        ck(sheet.opened === false, "settings: tapping X must close the sheet")
        ck(harness.dismissCount >= 1, "settings: tapping X must emit dismissed()")

        // --- dismiss: scrim tap (reopen first) ---
        sheet.open()
        ck(sheet.opened === true, "settings: reopen for scrim test")
        var scrim = byName(sheet, "settingsScrim")
        ck(scrim !== null, "settings: scrim must exist")
        clickCenter(scrim)
        ck(sheet.opened === false, "settings: scrim tap must close the sheet")

        // --- click-swallower: a body tap must NOT close ---
        sheet.open()
        var body = byName(sheet, "settingsBody")
        ck(body !== null, "settings: sheet body must exist")
        clickCenter(body)   // body swallows: no dismiss
        ck(sheet.opened === true, "settings: a tap on the sheet BODY must NOT dismiss (click-swallower law)")

        // --- close() API ---
        sheet.close()
        ck(sheet.opened === false, "settings: close() must close the sheet")
    }
    property int dismissCount: 0

    // ============================================================================================
    // SLICE 2 — the Pages filmstrip (Task 6)
    // ============================================================================================
    // The overlay talks to the BACKEND, not to a reader seam: pageCount / currentPage / order /
    // bookmarks are pushed in as plain properties and the only call it makes is imageUrl(page, tier).
    // The fake returns "" for every url on purpose — a real "image://comicreader/..." would warn once
    // per delegate against an engine with no such provider, and what this gate actually has to prove
    // is WHICH TIER was asked for, which the call log answers exactly.
    component FakePagesCore: QtObject {
        property var calls: []                       // [{page, tier}] — every imageUrl request
        property var bookmarksArr: []
        signal pageReady(int page)
        signal bookmarksChanged()
        function imageUrl(page, tier) {
            calls.push({ page: page, tier: String(tier) })
            return ""
        }
        function bookmarks() { return bookmarksArr.slice() }
        function tiersAsked() {
            var seen = {}
            for (var i = 0; i < calls.length; i++) seen[calls[i].tier] = true
            return Object.keys(seen).sort()
        }
    }

    FakePagesCore { id: fakePagesCore }

    property var pagesComp: null
    property var pages: null
    property int jumpCount: 0
    property int lastJump: -1
    property int pagesDismissCount: 0

    function runPages() {
        pages = pagesComp.createObject(harness, {
            "width": harness.width, "height": harness.height,
            "core": fakePagesCore, "pageCount": 230, "currentPage": 16,
            "order": "ltr", "bookmarks": [3, 15, 200]
        })
        var overlay = pages
        if (!overlay) { failures.push("pages: createObject returned null"); return }
        overlay.jumpRequested.connect(function (p) { harness.jumpCount += 1; harness.lastJump = p })
        overlay.dismissRequested.connect(function () { harness.pagesDismissCount += 1 })

        // --- closed by default, and a CLOSED filmstrip costs nothing ---
        // `open` is a RULE-level property on purpose: QQuickItem.visible is EFFECTIVE visibility, so
        // asserting on it would be reading the harness root's state as much as the overlay's.
        ck(overlay.open === false, "pages: must start CLOSED")
        ck(overlay.liveThumbs === 0, "pages: a closed filmstrip must instantiate ZERO thumbnails, got " + overlay.liveThumbs)

        // --- open it ---
        overlay.open = true
        overlay.centreNow()
        ck(overlay.open === true, "pages: open must be settable")

        // --- the current page is CENTERED (index) ---
        ck(overlay.centeredIndex === 15, "pages: current page 16 must centre index 15, got " + overlay.centeredIndex)
        // ...and the VIEW agrees. This is the value the ListView resets to 0 by itself when the
        // model goes 0 -> N, which is exactly what opening the surface does; a declarative binding
        // here is destroyed by that write, so the strip would centre page 1 forever.
        ck(overlay.flowCurrentIndex === 15,
           "pages: the view's own currentIndex must follow the reader, got " + overlay.flowCurrentIndex)

        // --- ...and centered in REAL GEOMETRY, not just in the component's own arithmetic ---
        var cx = overlay.itemCenterX(15)
        ck(!isNaN(cx), "pages: the centred thumbnail must be realized (got NaN centre)")
        ck(!isNaN(cx) && Math.abs(cx - overlay.viewportCenterX) <= 2.0,
           "pages: the current thumbnail must sit at the viewport centre, off by "
           + (isNaN(cx) ? "NaN" : Math.abs(cx - overlay.viewportCenterX).toFixed(2)) + "px")

        // --- scale grows toward the centre, and the DRAWN widths agree with the ramp ---
        ck(overlay.scaleForIndex(15) > overlay.scaleForIndex(16), "pages: the centre must outscale its right neighbour")
        ck(overlay.scaleForIndex(15) > overlay.scaleForIndex(14), "pages: the centre must outscale its left neighbour")
        ck(overlay.scaleForIndex(16) > overlay.scaleForIndex(18), "pages: a nearer neighbour must outscale a farther one")
        ck(overlay.itemWidthAt(15) > overlay.itemWidthAt(16),
           "pages: the centre delegate must be WIDER than its neighbour, got "
           + overlay.itemWidthAt(15) + " vs " + overlay.itemWidthAt(16))

        // --- VIRTUALIZED. This reader exists because of a stutter; 1,452 delegates is a new one. ---
        ck(overlay.liveThumbs > 0, "pages: an open filmstrip must instantiate SOME thumbnails")
        ck(overlay.liveThumbs < 60, "pages: 230 pages must not instantiate 230 thumbnails, got " + overlay.liveThumbs)
        overlay.pageCount = 1452
        overlay.currentPage = 700
        overlay.centreNow()
        ck(overlay.liveThumbs < 60, "pages: 1452 pages must not instantiate 1452 thumbnails, got " + overlay.liveThumbs)
        ck(overlay.centeredIndex === 699, "pages: centre must follow currentPage, got " + overlay.centeredIndex)
        var cx2 = overlay.itemCenterX(699)
        ck(!isNaN(cx2) && Math.abs(cx2 - overlay.viewportCenterX) <= 2.0,
           "pages: page 700 of 1452 must also land centred, off by "
           + (isNaN(cx2) ? "NaN" : Math.abs(cx2 - overlay.viewportCenterX).toFixed(2)) + "px")

        // --- thumbnails ride the THUMBNAIL tier. hq here would pull full-resolution scans. ---
        var tiers = fakePagesCore.tiersAsked()
        ck(tiers.length === 1 && tiers[0] === "thumbnail",
           "pages: every thumbnail request must use the 'thumbnail' tier, saw [" + tiers.join(",") + "]")

        // --- RTL mirrors the VISUAL sequence... ---
        overlay.pageCount = 230
        overlay.currentPage = 16
        overlay.order = "rtl"
        overlay.centreNow()
        ck(overlay.visualPageAt(0) > overlay.visualPageAt(1),
           "pages: RTL visual order must mirror, got " + overlay.visualPageAt(0) + " then " + overlay.visualPageAt(1))
        ck(overlay.visualPageAt(0) === 230 && overlay.visualPageAt(229) === 1,
           "pages: RTL must put the LAST page leftmost and the first page rightmost, got "
           + overlay.visualPageAt(0) + ".." + overlay.visualPageAt(229))
        // ...in the LAID-OUT delegates too, so the helper above is anchored to the real ListView and
        // is not merely restating its own arithmetic.
        ck(overlay.itemXAt(15) > overlay.itemXAt(16),
           "pages: under RTL the next page must be drawn to the LEFT, got x "
           + overlay.itemXAt(15) + " vs " + overlay.itemXAt(16))

        // --- ...and ONLY the visual sequence. The printed numbers never reverse their meaning. ---
        ck(overlay.labelTextAt(15) === "16", "pages: RTL page 16 must still be LABELLED 16, got '" + overlay.labelTextAt(15) + "'")
        ck(overlay.labelTextAt(16) === "17", "pages: RTL page 17 must still be LABELLED 17, got '" + overlay.labelTextAt(16) + "'")
        overlay.order = "ltr"
        overlay.centreNow()
        ck(overlay.visualPageAt(0) === 1 && overlay.visualPageAt(1) === 2,
           "pages: LTR visual order must run forward, got " + overlay.visualPageAt(0) + " then " + overlay.visualPageAt(1))
        ck(overlay.itemXAt(15) < overlay.itemXAt(16),
           "pages: under LTR the next page must be drawn to the RIGHT, got x "
           + overlay.itemXAt(15) + " vs " + overlay.itemXAt(16))
        ck(overlay.labelTextAt(15) === "16", "pages: LTR page 16 must be LABELLED 16, got '" + overlay.labelTextAt(15) + "'")

        // --- bookmarks mark the FILMSTRIP thumbnails (the rail's ticks are the chrome gate's half) ---
        ck(overlay.isBookmarked(15) === true, "pages: page index 15 is bookmarked")
        ck(overlay.isBookmarked(14) === false, "pages: page index 14 is not bookmarked")
        ck(overlay.markOpacityAt(15) > 0, "pages: a bookmarked thumbnail must SHOW its mark, got " + overlay.markOpacityAt(15))
        ck(overlay.markOpacityAt(14) === 0, "pages: an unbookmarked thumbnail must show no mark, got " + overlay.markOpacityAt(14))
        overlay.bookmarks = [14]
        ck(overlay.markOpacityAt(14) > 0 && overlay.markOpacityAt(15) === 0,
           "pages: the marks must follow a live bookmark change")
        overlay.bookmarks = [3, 15, 200]

        // --- JUMP: exactly one of each, through the PUBLIC door ---
        harness.jumpCount = 0; harness.pagesDismissCount = 0; harness.lastJump = -1
        overlay.activateIndex(18)
        ck(harness.jumpCount === 1 && harness.lastJump === 19 && harness.pagesDismissCount === 1,
           "pages: activateIndex(18) must jump ONCE to page 19 and dismiss ONCE, got jump=" + harness.jumpCount
           + " page=" + harness.lastJump + " dismiss=" + harness.pagesDismissCount)

        // --- JUMP: and through the DELEGATE's own door (the thing a click actually reaches) ---
        harness.jumpCount = 0; harness.pagesDismissCount = 0; harness.lastJump = -1
        ck(overlay.pressThumb(17) === true, "pages: thumbnail 17 must be realized and pressable")
        ck(harness.jumpCount === 1 && harness.lastJump === 18 && harness.pagesDismissCount === 1,
           "pages: a thumbnail press must jump ONCE to page 18 and dismiss ONCE, got jump=" + harness.jumpCount
           + " page=" + harness.lastJump + " dismiss=" + harness.pagesDismissCount)

        // --- an out-of-range index is inert, never a clamped jump to a page nobody asked for ---
        harness.jumpCount = 0; harness.pagesDismissCount = 0
        overlay.activateIndex(-1)
        overlay.activateIndex(9999)
        ck(harness.jumpCount === 0 && harness.pagesDismissCount === 0,
           "pages: an out-of-range activateIndex must emit NOTHING, got jump=" + harness.jumpCount
           + " dismiss=" + harness.pagesDismissCount)

        // --- DISMISS WITHOUT MOVING: the canvas catcher and dismiss() emit no jump at all ---
        var pageBefore = overlay.currentPage
        harness.jumpCount = 0; harness.pagesDismissCount = 0
        var catcher = byName(overlay, "pagesDismissCatcher")
        ck(catcher !== null, "pages: the canvas dismiss catcher must exist")
        if (catcher) catcher.tap()
        ck(harness.pagesDismissCount === 1, "pages: a click on the comic must dismiss, got " + harness.pagesDismissCount)
        ck(harness.jumpCount === 0, "pages: a click on the comic must emit NO jump, got " + harness.jumpCount)
        ck(overlay.currentPage === pageBefore,
           "pages: dismissal must not move the reading position, " + pageBefore + " -> " + overlay.currentPage)

        harness.jumpCount = 0; harness.pagesDismissCount = 0
        overlay.dismiss()
        ck(harness.pagesDismissCount === 1 && harness.jumpCount === 0,
           "pages: dismiss() must emit exactly one dismissRequested and no jump, got dismiss="
           + harness.pagesDismissCount + " jump=" + harness.jumpCount)
        ck(overlay.currentPage === pageBefore,
           "pages: dismiss() must not move the reading position, " + pageBefore + " -> " + overlay.currentPage)

        // --- the band swallows its own clicks: pressing the strip's empty ground is NOT a dismissal ---
        harness.jumpCount = 0; harness.pagesDismissCount = 0
        var band = byName(overlay, "pagesBand")
        ck(band !== null, "pages: the filmstrip band must exist")
        var swallow = byName(overlay, "pagesBandSwallow")
        ck(swallow !== null, "pages: the band must carry a click-swallower (floating-panel house law)")
        if (swallow) swallow.tap()
        ck(harness.pagesDismissCount === 0 && harness.jumpCount === 0,
           "pages: a tap on the band's empty ground must neither jump nor dismiss, got dismiss="
           + harness.pagesDismissCount + " jump=" + harness.jumpCount)

        // --- closing releases every delegate: a closed temporary surface holds nothing ---
        overlay.open = false
        ck(overlay.liveThumbs === 0, "pages: closing must release every thumbnail, got " + overlay.liveThumbs)
    }

    // ================= SLICE 3 — ComicReaderImagePopover (Task 7) =================
    // The approved shape, in Hemanth's words: "Image opens a compact anchored panel and does not
    // move the comic", with "contrast, gamma, rotation, and auto-crop behind one Advanced image
    // tools row". So what this slice pins is: three controls visible immediately, four more one
    // disclosure deeper IN THE SAME PANEL, every control emitting a COMPLETE profile map (the
    // backend REPLACES rather than merges — a partial map silently resets what it omits), and a
    // dismissal that changes nothing about the picture.
    property var imageComp: null
    property var image: null
    property int imageChangeCount: 0
    property var lastImageProfile: null
    property int imageDismissCount: 0

    // The live profile the shell pushes in — already normalised by the backend, which is the
    // shape the real shell hands over (its `renderProfile` readback).
    function freshProfile() {
        return { "brightness": 0, "contrast": 0, "gamma": 100, "rotation": 0,
                 "autoCrop": false, "nightFilter": false, "quality": "balanced" }
    }

    function runImage() {
        image = imageComp.createObject(harness, {
            "width": harness.width, "height": harness.height,
            "profile": freshProfile()
        })
        var panel = image
        if (!panel) { failures.push("image: createObject returned null"); return }
        panel.profileChangeRequested.connect(function (p) {
            harness.imageChangeCount += 1
            harness.lastImageProfile = p
        })
        panel.dismissRequested.connect(function () { harness.imageDismissCount += 1 })

        // --- closed by default, and the disclosure starts shut ---
        // `open` / `advancedOpen` are RULE-level properties on purpose: QQuickItem.visible is
        // EFFECTIVE visibility, so asserting on it would read the harness root's state too.
        ck(panel.open === false, "image: must start CLOSED")
        ck(panel.advancedOpen === false, "image: Advanced must start SHUT")

        panel.open = true
        ck(panel.open === true, "image: open must be settable")

        // --- the panel does not move the comic: it is a floating child, and the reading surfaces
        //     are not its business. What IS assertable here is that it hangs from the chrome and
        //     stays inside the reader rather than spilling off it. ---
        var body = byName(panel, "imagePanel")
        ck(body !== null, "image: the panel body must exist")
        if (body) {
            ck(body.y >= panel.chromeTopInset,
               "image: the panel must hang BELOW the command bar, y=" + body.y)
            ck(body.x + body.width <= panel.width + 0.5,
               "image: the panel must stay inside the reader, right edge "
               + (body.x + body.width) + " vs " + panel.width)
            ck(body.width <= 420, "image: the panel must be COMPACT, got width " + body.width)
        }

        // --- PRIMARY SURFACE: exactly Quality, Brightness, Night filter ---
        ck(byName(panel, "imageQuality_fast") !== null, "image: Quality must offer Fast")
        ck(byName(panel, "imageQuality_balanced") !== null, "image: Quality must offer Balanced")
        ck(byName(panel, "imageQuality_best") !== null, "image: Quality must offer Best")
        ck(byName(panel, "imageBrightness") !== null, "image: Brightness must be on the primary surface")
        ck(byName(panel, "imageNightFilter") !== null, "image: Night filter must be on the primary surface")

        // --- ...and the four ADVANCED controls live in the SAME panel, not a second surface ---
        var advanced = byName(panel, "imageAdvancedSection")
        ck(advanced !== null, "image: the Advanced section must exist")
        ck(byName(panel, "imageContrast") !== null, "image: Contrast belongs to Advanced")
        ck(byName(panel, "imageGamma") !== null, "image: Gamma belongs to Advanced")
        ck(byName(panel, "imageRotate_90") !== null, "image: Rotate belongs to Advanced")
        ck(byName(panel, "imageAutoCrop") !== null, "image: Auto-crop belongs to Advanced")
        if (advanced && body) {
            // "inside the same anchored panel" — structurally, not by eye.
            var owner = advanced.parent
            var inPanel = false
            while (owner) { if (owner === body) { inPanel = true; break } owner = owner.parent }
            ck(inPanel, "image: Advanced must expand INSIDE the same panel, not a second surface")
        }

        // --- the disclosure GROWS the panel rather than opening another one ---
        // forceLayout, exactly like the filmstrip gate: a positioner relayouts on POLISH, so a
        // synchronous read straight after the toggle would measure the pre-toggle column and the
        // assertion would be about the harness's timing rather than the panel's behaviour.
        var stack = byName(panel, "imagePanelColumn")
        ck(stack !== null, "image: the panel's content column must exist")
        var relayout = function () {
            if (advanced) advanced.forceLayout()
            if (stack) stack.forceLayout()
        }
        relayout()
        var heightShut = body ? body.height : 0
        panel.toggleAdvanced()
        relayout()
        ck(panel.advancedOpen === true, "image: the disclosure must open Advanced")
        ck(!body || body.height > heightShut,
           "image: opening Advanced must GROW the same panel, " + heightShut + " -> " + (body ? body.height : 0))
        panel.toggleAdvanced()
        relayout()
        ck(panel.advancedOpen === false, "image: the disclosure must close again")
        ck(!body || body.height === heightShut,
           "image: closing Advanced must return the panel to its compact height, got "
           + (body ? body.height : 0) + " vs " + heightShut)

        // --- EVERY control emits a COMPLETE map. This is the one that matters: setRenderProfile
        //     REPLACES, so a partial map would silently un-rotate the book. ---
        var required = ["brightness", "contrast", "gamma", "rotation", "autoCrop", "nightFilter", "quality"]
        harness.imageChangeCount = 0
        panel.setBrightness(30)
        ck(harness.imageChangeCount === 1, "image: setBrightness must emit exactly one change, got " + harness.imageChangeCount)
        var missing = []
        for (var i = 0; i < required.length; i++)
            if (!harness.lastImageProfile || harness.lastImageProfile[required[i]] === undefined)
                missing.push(required[i])
        ck(missing.length === 0, "image: every change must carry a COMPLETE profile; missing [" + missing.join(",") + "]")
        ck(harness.lastImageProfile.brightness === 30,
           "image: brightness must reach the map, got " + harness.lastImageProfile.brightness)

        // A change must PRESERVE every other field of the live profile — the exact failure a
        // partial map produces, asserted against a profile that is NOT all defaults.
        panel.profile = { "brightness": 0, "contrast": -20, "gamma": 140, "rotation": 90,
                          "autoCrop": true, "nightFilter": true, "quality": "best" }
        panel.setBrightness(-15)
        ck(harness.lastImageProfile.contrast === -20 && harness.lastImageProfile.gamma === 140
           && harness.lastImageProfile.rotation === 90 && harness.lastImageProfile.autoCrop === true
           && harness.lastImageProfile.nightFilter === true && harness.lastImageProfile.quality === "best",
           "image: changing ONE field must carry every other field through unchanged, got "
           + JSON.stringify(harness.lastImageProfile))

        // --- each control writes its own field, and clamps at the panel's own door ---
        panel.profile = freshProfile()
        panel.setQuality("best")
        ck(harness.lastImageProfile.quality === "best", "image: quality must be settable")
        harness.imageChangeCount = 0
        panel.setQuality("nonsense")
        ck(harness.imageChangeCount === 0, "image: an unknown quality must be INERT, not a fallthrough")
        panel.setContrast(999)
        ck(harness.lastImageProfile.contrast === 100, "image: contrast clamps to 100, got " + harness.lastImageProfile.contrast)
        panel.setBrightness(-999)
        ck(harness.lastImageProfile.brightness === -100, "image: brightness clamps to -100, got " + harness.lastImageProfile.brightness)
        panel.setGamma(0)
        ck(harness.lastImageProfile.gamma === 10, "image: gamma clamps UP to 10, never a black page, got " + harness.lastImageProfile.gamma)
        panel.setGamma(9999)
        ck(harness.lastImageProfile.gamma === 300, "image: gamma clamps to 300, got " + harness.lastImageProfile.gamma)
        panel.setRotation(45)
        ck(harness.lastImageProfile.rotation === 90, "image: an off-grid rotation snaps, got " + harness.lastImageProfile.rotation)
        panel.setRotation(-90)
        ck(harness.lastImageProfile.rotation === 270, "image: -90 folds to 270, got " + harness.lastImageProfile.rotation)
        panel.setNightFilter(true)
        ck(harness.lastImageProfile.nightFilter === true, "image: the night filter must be settable")
        panel.setAutoCrop(true)
        ck(harness.lastImageProfile.autoCrop === true, "image: auto-crop must be settable")

        // --- the controls REFLECT the live profile (a chip can only mark itself active if it can
        //     read the value back) ---
        panel.profile = { "brightness": 12, "contrast": -8, "gamma": 250, "rotation": 180,
                          "autoCrop": true, "nightFilter": true, "quality": "fast" }
        ck(panel.quality === "fast" && panel.brightness === 12 && panel.contrast === -8
           && panel.gamma === 250 && panel.rotationDegrees === 180 && panel.autoCrop === true
           && panel.nightFilter === true,
           "image: the panel must read the live profile back")
        var fastChip = byName(panel, "imageQuality_fast")
        var bestChip = byName(panel, "imageQuality_best")
        ck(fastChip && fastChip.active === true, "image: the active quality chip must be marked")
        ck(bestChip && bestChip.active === false, "image: only the active quality chip is marked")
        var turn180 = byName(panel, "imageRotate_180")
        var turn0 = byName(panel, "imageRotate_0")
        ck(turn180 && turn180.active === true, "image: the active rotation chip must be marked")
        ck(turn0 && turn0.active === false, "image: only the active rotation chip is marked")

        // --- a PARTIAL or absent profile degrades to the defaults, never to NaN/0. gamma 0 would
        //     be a black page, and an undefined slider value is a handle halfway off the track. ---
        panel.profile = {}
        ck(panel.gamma === 100 && panel.brightness === 0 && panel.rotationDegrees === 0
           && panel.quality === "balanced",
           "image: an EMPTY profile must read as the defaults, got gamma " + panel.gamma)
        panel.profile = { "gamma": "junk", "brightness": null }
        ck(panel.gamma === 100 && panel.brightness === 0,
           "image: an unparseable profile must read as the defaults, got gamma " + panel.gamma)

        // --- the slider's value mapping is PURE and harness-callable, so the pointer path cannot
        //     drift from a separately-described one ---
        var slider = byName(panel, "imageBrightness")
        if (slider) {
            ck(slider.valueAt(0) === slider.from, "image: a press at the far left means `from`")
            ck(Math.abs(slider.valueAt(1e6) - slider.to) < 1e-6, "image: a press past the far right clamps to `to`")
            ck(slider.fractionOf(slider.from) === 0 && slider.fractionOf(slider.to) === 1,
               "image: the handle fraction spans the whole track")
            // the handle FOLLOWS the profile while it is not held...
            panel.profile = freshProfile()
            ck(slider.value === 0, "image: the handle must follow the live profile, got " + slider.value)
            // ...and does NOT get yanked back mid-drag by a lagging readback (the throttle means
            // the profile trails the gesture, and a handle that snapped backwards would be
            // unusable).
            slider.held = true
            slider.moveTo(45)
            panel.profile = freshProfile()          // a stale readback lands mid-drag
            ck(slider.value === 45, "image: a held handle must not be yanked back by a stale readback, got " + slider.value)
            slider.held = false
        }

        // --- DISMISS NEVER CHANGES THE PICTURE. The catcher and dismiss() emit dismissRequested
        //     and nothing else; there is no path from either to a profile change. ---
        harness.imageChangeCount = 0
        harness.imageDismissCount = 0
        var catcher = byName(panel, "imageDismissCatcher")
        ck(catcher !== null, "image: a click on the comic must be catchable")
        if (catcher) catcher.tap()
        ck(harness.imageDismissCount === 1 && harness.imageChangeCount === 0,
           "image: clicking the comic must dismiss and change NOTHING, got dismiss="
           + harness.imageDismissCount + " change=" + harness.imageChangeCount)
        harness.imageDismissCount = 0
        panel.dismiss()
        ck(harness.imageDismissCount === 1 && harness.imageChangeCount === 0,
           "image: dismiss() must emit exactly one dismissRequested and no change")

        // --- the panel swallows its own clicks (floating-panel house law) ---
        harness.imageDismissCount = 0
        var swallow = byName(panel, "imagePanelSwallow")
        ck(swallow !== null, "image: the panel must carry a click-swallower")
        if (swallow) swallow.tap()
        ck(harness.imageDismissCount === 0,
           "image: a tap on the panel's own ground must NOT dismiss it, got " + harness.imageDismissCount)

        // --- closing resets the disclosure: the panel's promise is three controls on open ---
        panel.toggleAdvanced()
        panel.open = false
        ck(panel.advancedOpen === false, "image: closing must reset the Advanced disclosure")
    }

    function runChecks() {
        try { runSettings() }
        catch (e) { failures.push("exception: " + e.message) }
        try { runPages() }
        catch (e) { failures.push("pages exception: " + e.message) }
        try { runImage() }
        catch (e) { failures.push("image exception: " + e.message) }
        report()
    }

    Component.onCompleted: {
        try {
            sheetComp = Qt.createComponent("../qml/comicreader/ComicReaderSettingsSheet.qml")
            if (sheetComp.status === Component.Error) throw new Error("settings component: " + sheetComp.errorString())
            pagesComp = Qt.createComponent("../qml/comicreader/ComicReaderPagesOverlay.qml")
            if (pagesComp.status === Component.Error) throw new Error("pages component: " + pagesComp.errorString())
            imageComp = Qt.createComponent("../qml/comicreader/ComicReaderImagePopover.qml")
            if (imageComp.status === Component.Error) throw new Error("image component: " + imageComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_OVERLAYS_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    Timer { interval: 8000; running: true; onTriggered: { console.log("COMICREADER_OVERLAYS_FAIL: timeout"); Qt.exit(1) } }
}

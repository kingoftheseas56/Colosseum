import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault Browse face execution plan, Slice 5 — the assembled face (carousel, collapsible rail,
// breadcrumb, media-faced grid) driven with a SEEDED PROJECTION STUB standing in for
// VaultLibrary.
//
// VaultPage.qml itself reads a real C++ singleton throughout (`typeof VaultLibrary !==
// "undefined"` guards) that the Quick Test engine never registers — the same structural reason
// `OpenRecentPanel.qml` was extracted from Main.qml to be independently seedable (ledger
// precedent, tst_open_recent_panel.qml). This harness wires the SAME production sub-components
// VaultPage.qml assembles — `FeaturedCarousel`, `VaultBrowseRail`, `VaultBrowseCrumb`,
// `VaultPosterCard`, `VaultWideCard` — through a small local replica of VaultPage's own
// navigation state machine (pushCrumb/goToCrumb/handleBrowseCardOpen), fed by a seeded
// browseAt()-shaped stub instead of the real index. The end-to-end proof against the ASSEMBLED
// page with the REAL VaultLibrary is the Lanista replay (vault_browse_smoke.json), not this
// layer — this layer proves the reusable components + navigation contract are correct.
TestCase {
    id: testCase
    name: "VaultBrowsePage"
    when: windowShown

    Window { id: testWindow; width: 1100; height: 760; visible: true }

    // ---- seeded projection stub: path -> rows (mirrors VaultLibrary.browseAt()'s row shape) ----
    property var levelData: ({})
    function browseAt(path) { return testCase.levelData[path] || [] }
    // Replace one level's rows and reassign `levelData` itself (not a nested mutation) — QML
    // only re-evaluates a `var` binding when a PROPERTY it read is REASSIGNED, not when an
    // object the property currently points at is mutated in place. Mirrors the real bug this
    // same reasoning avoids in production: browseGridRows must genuinely CHANGE for
    // onBrowseGridRowsChanged to fire.
    function reprojectLevel(path, rows) {
        var updated = {}
        for (var p in testCase.levelData) updated[p] = testCase.levelData[p]
        updated[path] = rows
        testCase.levelData = updated
    }

    property var crumbStack: []
    property string currentBrowsePath: ""
    property string lastOpenedPath: ""
    readonly property var browseGridRows: testCase.currentBrowsePath ? testCase.browseAt(testCase.currentBrowsePath) : []
    readonly property bool browseGridWide: testCase.browseGridRows.length > 0
        && (testCase.browseGridRows[0].nodeType === "episode" || testCase.browseGridRows[0].nodeType === "clip")

    // ---- Slice 7: the detail sheet — seeded browseDetail()-shaped stub, mirroring VaultPage's
    //      own detailSheetVisible/detailSheetKey/detailSheetDetail wiring so a pass here is real
    //      evidence for the production seam, not a simplified stand-in. ----
    property var detailData: ({})
    function browseDetail(key) { return testCase.detailData[key] || ({ found: false }) }
    property bool detailSheetVisible: false
    property string detailSheetKey: ""
    property string detailSheetRowState: ""
    readonly property var detailSheetDetail: testCase.detailSheetVisible && testCase.detailSheetKey
        ? testCase.browseDetail(testCase.detailSheetKey) : ({})
    function openDetailSheet(row) {
        testCase.detailSheetKey = row.key || ""
        testCase.detailSheetRowState = row.state || ""
        testCase.detailSheetVisible = true
    }
    function closeDetailSheet() { testCase.detailSheetVisible = false }

    // ==== Slice 6 mirror of VaultPage.qml's key-stable re-projection — see that file's own
    //      comment block for the full "why" (a plain array `model:` binding rebuilds the whole
    //      grid on every recompute; this harness must prove the SAME mechanism the shipped page
    //      uses, not a simplified stand-in, or a pass here would not be evidence for real. ====
    onBrowseGridRowsChanged: testCase.syncGridModel(testCase.browseGridRows)
    property string gridSyncedLevelKey: " __unsynced__"
    function syncGridModel(rows) {
        rows = rows || []
        const levelChanged = testCase.currentBrowsePath !== testCase.gridSyncedLevelKey
        testCase.gridSyncedLevelKey = testCase.currentBrowsePath
        var structurallySame = !levelChanged && gridModel.count === rows.length
        if (structurallySame) {
            for (var i = 0; i < rows.length; ++i) {
                if (gridModel.get(i).key !== (rows[i].key || "")) { structurallySame = false; break }
            }
        }
        if (structurallySame) {
            for (var k = 0; k < rows.length; ++k)
                gridModel.set(k, { key: rows[k].key || "", modelData: rows[k] })
        } else {
            gridModel.clear()
            for (var j = 0; j < rows.length; ++j)
                gridModel.append({ key: rows[j].key || "", modelData: rows[j] })
        }
    }

    function selectRoot(path, name) {
        testCase.crumbStack = [{ key: path, displayTitle: name }]
        testCase.currentBrowsePath = path
    }
    function pushCrumb(key, title) {
        testCase.crumbStack = testCase.crumbStack.concat([{ key: key, displayTitle: title }])
        testCase.currentBrowsePath = key
    }
    function goToCrumb(index) {
        if (index < 0 || index >= testCase.crumbStack.length - 1) return
        testCase.crumbStack = testCase.crumbStack.slice(0, index + 1)
        testCase.currentBrowsePath = testCase.crumbStack[testCase.crumbStack.length - 1].key
    }
    // Slice 9 — Backspace ascends. This harness has no browseFace wrapper (production's
    // Keys.onPressed lives one level up from the grid); mirroring the OBSERVABLE contract is
    // what matters, so `grid` itself catches Backspace directly here (see its own Keys.onPressed
    // below), same effect as production's bubble-to-parent.
    function ascendBrowse() {
        if (testCase.crumbStack.length > 1) testCase.goToCrumb(testCase.crumbStack.length - 2)
    }
    // Slice 9 — Enter opens whichever card the grid's keyboard traversal currently focuses,
    // through the SAME routing a mouse click already uses.
    function openFocusedGridCard() {
        if (grid.currentIndex < 0 || grid.currentIndex >= gridModel.count) return
        const rec = gridModel.get(grid.currentIndex)
        if (rec && rec.modelData) testCase.handleBrowseCardOpen(rec.modelData)
    }
    function handleBrowseCardOpen(row) {
        if (!row) return
        if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
            testCase.pushCrumb(row.key, row.displayTitle)
            return
        }
        if (row.nodeType === "film") {
            testCase.openDetailSheet(row)
            return
        }
        testCase.lastOpenedPath = row.path || ""
    }

    Component {
        id: posterDelegateComp
        Colosseum.VaultPosterCard {
            required property var modelData
            row: modelData
            onOpenRequested: (r) => testCase.handleBrowseCardOpen(r)
        }
    }
    Component {
        id: wideDelegateComp
        Colosseum.VaultWideCard {
            required property var modelData
            row: modelData
            onOpenRequested: (r) => testCase.handleBrowseCardOpen(r)
        }
    }

    property var carouselSlidesSeed: []
    Colosseum.FeaturedCarousel {
        id: carousel
        objectName: "vaultBrowseCarousel"
        parent: testWindow.contentItem
        width: 900
        slides: testCase.carouselSlidesSeed
    }

    property var rootsSeed: []
    Colosseum.VaultBrowseRail {
        id: rail
        parent: testWindow.contentItem
        y: 340
        roots: testCase.rootsSeed
        onToggleRequested: rail.expanded = !rail.expanded
        onRootSelected: (path) => testCase.selectRoot(path, path)
    }

    Colosseum.VaultBrowseCrumb {
        id: crumb
        objectName: "vaultBrowseCrumb"
        parent: testWindow.contentItem
        x: 300; y: 340; width: 500
        stack: testCase.crumbStack
        onSegmentClicked: (index) => testCase.goToCrumb(index)
    }

    ListModel { id: gridModel }
    SignalSpy { id: cardCrossfadeSpy; signalName: "faceCrossfaded" }

    GridView {
        id: grid
        objectName: "vaultBrowseGrid"
        parent: testWindow.contentItem
        x: 300; y: 400
        width: 700; height: 320
        clip: true
        // Slice 8: production's own cacheBuffer (VaultPage.qml, Gintama-scale headroom) — the
        // virtualization test below must prove recycling under the SAME setting that ships, not
        // a more forgiving one invented for the harness.
        cacheBuffer: 900
        cellWidth: testCase.browseGridWide ? 320 : 170
        cellHeight: testCase.browseGridWide ? 250 : 300
        model: gridModel
        delegate: testCase.browseGridWide ? wideDelegateComp : posterDelegateComp
        // Slice 9 — mirrors VaultPage.qml's own grid wiring exactly: focus ring on keyboard
        // focus only, Enter opens the focused card, Backspace ascends, Tab reaches the rail.
        activeFocusOnTab: true
        KeyNavigation.tab: rail
        onActiveFocusChanged: if (grid.activeFocus && grid.currentIndex < 0 && grid.count > 0)
                                  grid.currentIndex = 0
        highlight: Rectangle {
            color: "transparent"; radius: 8; border.width: 2; border.color: "#c9c8d0" // theme.inkDim
            visible: grid.activeFocus
        }
        Keys.onReturnPressed: (event) => { testCase.openFocusedGridCard(); event.accepted = true }
        Keys.onEnterPressed: (event) => { testCase.openFocusedGridCard(); event.accepted = true }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backspace) { testCase.ascendBrowse(); event.accepted = true }
        }
    }

    // ── Slice 9: the empty-state family, seeded directly (no live grid needed to prove copy) ──
    property string emptyCauseSeed: ""
    property int emptyItemsCountSeed: 0
    Colosseum.VaultBrowseEmpty {
        id: emptyState
        objectName: "vaultBrowseGridEmpty"
        parent: testWindow.contentItem
        x: 300; y: 400; width: 700; height: 320
        cause: testCase.emptyCauseSeed
        itemsCount: testCase.emptyItemsCountSeed
    }

    Colosseum.VaultDetailSheet {
        id: sheet
        objectName: "vaultBrowseSheet"
        parent: testWindow.contentItem
        z: 100
        visible: testCase.detailSheetVisible
        detail: testCase.detailSheetDetail
        identityStateOfRow: testCase.detailSheetRowState
        onBackRequested: testCase.closeDetailSheet()
        onPlayRequested: (path) => {
            testCase.closeDetailSheet()
            testCase.lastOpenedPath = path || ""
        }
    }

    function init() {
        testCase.crumbStack = []
        testCase.currentBrowsePath = ""
        testCase.lastOpenedPath = ""
        testCase.levelData = ({})
        testCase.detailData = ({})
        testCase.detailSheetVisible = false
        testCase.detailSheetKey = ""
        testCase.detailSheetRowState = ""
        testCase.rootsSeed = []
        testCase.carouselSlidesSeed = []
        testCase.gridSyncedLevelKey = " __unsynced__"
        gridModel.clear()
        cardCrossfadeSpy.clear()
        cardCrossfadeSpy.target = null
        rail.expanded = false
        testCase.emptyCauseSeed = ""
        testCase.emptyItemsCountSeed = 0
        grid.currentIndex = -1
        wait(20)
    }

    // ── 1. the grid populates one card per row ──────────────────────────────────────────────
    function test_grid_populates_one_card_per_row() {
        testCase.levelData["/root"] = [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "2021 · 1080p", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "identified", away: false },
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "1080p · 2 seasons",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: false },
            { key: "/root/Cricket", nodeType: "folder", displayTitle: "Cricket", physicalFact: "4 items",
              path: "/root/Cricket", counts: { items: 4 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        compare(grid.count, 3)
        verify(findChild(grid, "vaultBrowseCard_/root/Spider-Man") !== null)
        verify(findChild(grid, "vaultBrowseCard_/root/Loki") !== null)
        verify(findChild(grid, "vaultBrowseCard_/root/Cricket") !== null)
    }

    // ── 2. NEGATIVE CONTROL: an EMPTY projection yields grid count 0 — proves the populate
    //      assertion above can actually fail (mandatory per the execution plan). ─────────────
    function test_empty_projection_yields_zero_count() {
        testCase.levelData["/empty"] = []
        testCase.selectRoot("/empty", "Empty root")
        wait(80)
        compare(grid.count, 0)
    }

    // ── 3. drill: opening a container card pushes a crumb row and changes the path ─────────
    function test_drill_pushes_crumb_and_changes_path() {
        testCase.levelData["/root"] = [
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "1080p · 2 seasons",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.levelData["/root/Loki"] = []
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        compare(crumb.stack.length, 1)
        const art = findChild(grid, "vaultBrowseCard_/root/Loki_art")
        verify(art !== null)
        mouseClick(art)
        wait(80)
        compare(testCase.currentBrowsePath, "/root/Loki")
        compare(crumb.stack.length, 2)
        compare(crumb.currentPath, "/root/Loki")
        compare(crumb.stack[1].displayTitle, "Loki")
    }

    // ── 4. rail is collapsed by default; the toggle expands it; a seeded root row renders ──
    function test_rail_collapsed_by_default_and_toggle_expands() {
        testCase.rootsSeed = [
            { path: "/root", name: "hemanth's folder", available: true, itemCount: 8, fileCount: 404 }
        ]
        wait(40)
        compare(rail.expanded, false)
        compare(rail.width, 62)
        const rootRow = findChild(rail, "vaultBrowseRailRoot_0")
        verify(rootRow !== null)
        compare(rootRow.available, true)
        const toggle = findChild(rail, "vaultBrowseRailToggle")
        verify(toggle !== null)
        mouseClick(toggle)
        tryCompare(rail, "expanded", true, 600)
        wait(250)   // let the width Behavior (160ms) settle before asserting the expanded size
        compare(rail.width, 236)
    }

    // ── 5. the carousel is present with >=1 slide when arrivals are non-empty; the blurb slot
    //      carries the PHYSICAL FACT only (design §4.10 — a descriptive blurb is a tagline) ──
    function test_carousel_present_with_slides_when_arrivals_nonempty() {
        testCase.carouselSlidesSeed = [
            { title: "Loki", blurb: "Season 2 · 1080p", ghost: "TV",
              c1: Qt.rgba(1, 1, 1, 0.10), c2: Qt.rgba(1, 1, 1, 0.025), art: "", artKind: "poster" }
        ]
        wait(60)
        compare(carousel.objectName, "vaultBrowseCarousel")
        compare(carousel.slides.length, 1)
        compare(carousel.slides[0].title, "Loki")
        verify(findText(carousel, "Loki") !== null)
        verify(findText(carousel, "Season 2 · 1080p") !== null)
    }

    // ── 6. breadcrumb middle-collapse (design §4.5): first and last always visible ─────────
    function test_crumb_middle_segments_collapse_when_far_too_many() {
        testCase.crumbStack = [
            { key: "/r", displayTitle: "hemanth's folder" },
            { key: "/r/a", displayTitle: "A" },
            { key: "/r/a/b", displayTitle: "B" },
            { key: "/r/a/b/c", displayTitle: "C" },
            { key: "/r/a/b/c/d", displayTitle: "D" }
        ]
        wait(40)
        compare(crumb.displaySegments.length, 4)   // first, ellipsis, second-last, last
        compare(crumb.displaySegments[0].title, "hemanth's folder")
        compare(crumb.displaySegments[1].collapsed, true)
        compare(crumb.displaySegments[crumb.displaySegments.length - 1].title, "D")
    }

    // ── 7. Slice 6 — a same-folder re-projection updates ONE card in place; the grid does NOT
    //      rebuild (delegate identity stable), matching design §4.4 ("the tile animates to its
    //      new position rather than teleporting") and the execution plan's own key-stability
    //      requirement. This is the signature assertion of the slice. ─────────────────────────
    function test_reproject_same_folder_updates_one_card_without_rebuilding_grid() {
        testCase.levelData["/root"] = [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "resolving", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "resolving", away: false },
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "1080p · 2 seasons",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        compare(grid.count, 2)

        var spiderCardBefore = findChild(grid, "vaultBrowseCard_/root/Spider-Man")
        var lokiCardBefore = findChild(grid, "vaultBrowseCard_/root/Loki")
        verify(spiderCardBefore !== null)
        verify(lokiCardBefore !== null)
        compare(spiderCardBefore.faceState, "filename")

        cardCrossfadeSpy.clear()
        cardCrossfadeSpy.target = spiderCardBefore

        // Same key set, same order — only the Spider-Man row's state/title changed (identify
        // settled it). Loki's row is byte-identical.
        testCase.reprojectLevel("/root", [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "2021 · 1080p", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "identified", away: false },
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "1080p · 2 seasons",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: false }
        ])
        tryCompare(cardCrossfadeSpy, "count", 1, 600)
        compare(spiderCardBefore.faceState, "settled")

        // The KEY assertion: the grid did not rebuild. Both delegate Items are the SAME object
        // references as before the re-projection (identity, not just equal content).
        var spiderCardAfter = findChild(grid, "vaultBrowseCard_/root/Spider-Man")
        var lokiCardAfter = findChild(grid, "vaultBrowseCard_/root/Loki")
        verify(spiderCardAfter === spiderCardBefore)
        verify(lokiCardAfter === lokiCardBefore)
        compare(grid.count, 2)
        cardCrossfadeSpy.target = null
    }

    // ── 8. Slice 6 — a stub away-flip: the card enters away in place (same delegate identity)
    //      and its open signal goes inert. Mandatory negative control performed and restored
    //      live during verification (see the slice report), not left flipped in committed code. ──
    function test_away_flip_enters_away_and_open_signal_goes_inert() {
        testCase.levelData["/root"] = [
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "1080p · 2 seasons",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        var cardBefore = findChild(grid, "vaultBrowseCard_/root/Loki")
        verify(cardBefore !== null)
        compare(cardBefore.away, false)
        var hitAreaBefore = findChild(grid, "vaultBrowseCard_/root/Loki_hitArea")
        verify(hitAreaBefore !== null)
        verify(hitAreaBefore.enabled)

        testCase.reprojectLevel("/root", [
            { key: "/root/Loki", nodeType: "show", displayTitle: "Loki", physicalFact: "Drive not connected",
              path: "/root/Loki", counts: { items: 2 }, coverRef: "", state: "identified", away: true }
        ])
        wait(80)

        var cardAfter = findChild(grid, "vaultBrowseCard_/root/Loki")
        verify(cardAfter === cardBefore)   // same delegate — away is a re-project, not a rebuild
        compare(cardAfter.away, true)
        var hitArea = findChild(grid, "vaultBrowseCard_/root/Loki_hitArea")
        verify(hitArea !== null)
        // the correct, positive assertion: away structurally disables the hit area (no hover, no
        // click) — design §4.3/§6.2 ("away = reduced ink + desaturation, no hover").
        compare(hitArea.enabled, false)
        testCase.lastOpenedPath = ""
        mouseClick(hitArea)
        wait(40)
        compare(testCase.lastOpenedPath, "")   // the open signal never fired
    }

    // ── 9. Slice 7 — clicking a Film card opens the detail sheet (not a direct Play), and it
    //      renders the seeded copy rows, companion chips, and evidence text. ─────────────────
    function test_film_card_click_opens_sheet_with_seeded_detail() {
        testCase.levelData["/root"] = [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "1080p WEBRip", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.detailData["/root/Spider-Man"] = {
            found: true, key: "/root/Spider-Man", displayTitle: "Spider-Man: No Way Home",
            year: 2021, identityState: "identified", identityLabel: "identity certain",
            copiesHeld: 1, bestQualityLine: "1080p WEBRip",
            copies: [ { path: "/root/Spider-Man/spiderman.mp4", rootPath: "/root",
                        quality: "1080p WEBRip", sizeBytes: 2254857830, sizeText: "2.1 GB",
                        where: "hemanth's folder / Spider-Man No Way Home", away: false } ],
            companions: [ "Subtitle (.SRT)", "Subs · 2 files" ],
            extras: [ { title: "Spider-Man No Way Home Trailer", path: "/root/Spider-Man/Extras/trailer.mp4" } ],
            evidence: "Filename parsed to Spider-Man: No Way Home with year 2021. One matching title was found, and nothing here is overriding you.",
            playPath: "/root/Spider-Man/spiderman.mp4"
        }
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        const art = findChild(grid, "vaultBrowseCard_/root/Spider-Man_art")
        verify(art !== null)
        mouseClick(art)
        wait(80)

        compare(testCase.detailSheetVisible, true)
        tryCompare(sheet, "visible", true, 600)
        const copyRow = findChild(sheet, "vaultBrowseSheetCopy_0")
        verify(copyRow !== null)
        const evidenceText = findChild(sheet, "vaultBrowseSheetEvidence")
        verify(evidenceText !== null)
        verify(("" + evidenceText.text).indexOf("Spider-Man") >= 0)
        verify(findText(sheet, "Subtitle (.SRT)") !== null)
        verify(findText(sheet, "Subs · 2 files") !== null)
    }

    // ── 10. Slice 7 — Play emits the CONCRETE file path (never merely non-empty; Slice 5 found
    //      a real bug where a Film node carried its containing FOLDER instead of the file). ──
    function test_sheet_play_emits_with_exact_path() {
        testCase.levelData["/root"] = [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "1080p WEBRip", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.detailData["/root/Spider-Man"] = {
            found: true, key: "/root/Spider-Man", displayTitle: "Spider-Man: No Way Home",
            copies: [], companions: [], extras: [], evidence: "",
            playPath: "/root/Spider-Man/spiderman.mp4"
        }
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        mouseClick(findChild(grid, "vaultBrowseCard_/root/Spider-Man_art"))
        wait(80)
        const playBtn = findChild(sheet, "vaultBrowseSheetPlay")
        verify(playBtn !== null)
        testCase.lastOpenedPath = ""
        mouseClick(playBtn)
        wait(60)
        compare(testCase.lastOpenedPath, "/root/Spider-Man/spiderman.mp4")
        compare(testCase.detailSheetVisible, false)   // Play closes the sheet
    }

    // ── 11. Slice 7 — Escape dismisses the sheet without opening media. ─────────────────────
    function test_sheet_escape_dismisses() {
        testCase.levelData["/root"] = [
            { key: "/root/Spider-Man", nodeType: "film", displayTitle: "Spider-Man: No Way Home",
              physicalFact: "1080p WEBRip", path: "/root/Spider-Man/spiderman.mp4",
              counts: { items: 1 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.detailData["/root/Spider-Man"] = {
            found: true, key: "/root/Spider-Man", displayTitle: "Spider-Man: No Way Home",
            copies: [], companions: [], extras: [], evidence: "",
            playPath: "/root/Spider-Man/spiderman.mp4"
        }
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        mouseClick(findChild(grid, "vaultBrowseCard_/root/Spider-Man_art"))
        wait(80)
        compare(testCase.detailSheetVisible, true)
        testWindow.requestActivate()
        sheet.forceActiveFocus()
        wait(40)
        verify(sheet.activeFocus)
        keyClick(Qt.Key_Escape)
        wait(60)
        compare(testCase.detailSheetVisible, false)
        compare(testCase.lastOpenedPath, "")   // dismiss never opens media
    }

    // ── 12. Slice 8 — a show's seasons band renders in NATURAL numeric order (1, 2, ... 10),
    //      never lexical ("1, 10, 2, ..."). The C++ planner already sorts numerically
    //      (tst_vault_kit's Loki/Wire cases); this proves the QML layer's own re-projection
    //      (syncGridModel) never re-sorts or scrambles what it was handed. ─────────────────────
    function test_season_band_renders_in_natural_numeric_order() {
        var seasons = []
        for (var s = 1; s <= 10; ++s) {
            seasons.push({
                key: "/root/Show::show::gintama/Season " + s, nodeType: "season",
                displayTitle: "Season " + s, physicalFact: (s * 3) + " episodes",
                path: "/root/Show/Season " + s, counts: { items: s * 3 }, coverRef: "",
                state: "identified", away: false
            })
        }
        testCase.levelData["/root/Show::show::gintama"] = seasons
        testCase.selectRoot("/root/Show::show::gintama", "Gintama")
        wait(80)
        compare(grid.count, 10)
        // Read the grid's OWN backing model in rendered order — not the seed array — so a
        // regression that resorts inside syncGridModel would be caught here too.
        for (var i = 0; i < 10; ++i)
            compare(gridModel.get(i).modelData.displayTitle, "Season " + (i + 1))
        // The lexical-risk pair a string sort would get wrong: "Season 10" must render AFTER
        // "Season 2", not before it.
        var idx2 = -1, idx10 = -1
        for (var j = 0; j < 10; ++j) {
            if (gridModel.get(j).modelData.displayTitle === "Season 2") idx2 = j
            if (gridModel.get(j).modelData.displayTitle === "Season 10") idx10 = j
        }
        verify(idx2 >= 0 && idx10 >= 0)
        verify(idx2 < idx10)
    }

    // ── 13. Slice 8 — the virtualization proof: a 300-episode wide-card wall renders WITHOUT
    //      instantiating all 300 delegates (Gintama scale is real — 367 episodes on the real
    //      disk). `grid.count` is the full model total; `grid.contentItem.children.length` is
    //      how many delegate Items actually exist right now — the count comparison IS the
    //      virtualization assertion, not a nicety. ──────────────────────────────────────────────
    function test_wide_grid_virtualizes_300_episodes_without_instantiating_all() {
        var episodes = []
        for (var e = 1; e <= 300; ++e) {
            var n = String(e).padStart(3, "0")
            episodes.push({
                key: "/root/Gintama/ep" + n, nodeType: "episode",
                displayTitle: "Gintama - " + n, physicalFact: "Episode " + e + " · 1080p",
                path: "/root/Gintama/ep" + n + ".mkv", counts: { items: 0 }, coverRef: "",
                state: "identified", away: false
            })
        }
        testCase.levelData["/root/Gintama"] = episodes
        testCase.selectRoot("/root/Gintama", "Gintama")
        wait(120)
        compare(grid.count, 300)                    // the full model — every real file counted
        compare(testCase.browseGridWide, true)       // episodes render as 16:9 wide cards
        var created = grid.contentItem.children.length
        verify(created > 0)                          // something rendered
        verify(created < 300)                         // NOT every delegate was instantiated
        verify(created < 60)                          // genuinely bounded, not "299 vs 300"
    }

    // ── 14. Slice 9 — each empty cause renders its OWN copy; four distinct exact strings, and
    //      the "noRoots" cause is the only one that shows the Add-storage affordance. ──────────
    function test_empty_states_render_four_distinct_exact_copies() {
        testCase.emptyCauseSeed = "noRoots"
        wait(20)
        compare(findChild(emptyState, "vaultBrowseEmptyHeading").text, "No storage yet")
        compare(findChild(emptyState, "vaultBrowseEmptyBody").text,
                "Point Vault at a folder or a drive and it will work out what is there.")
        verify(findChild(emptyState, "vaultBrowseEmptyAddStorage") !== null)
        verify(findChild(emptyState, "vaultBrowseEmptyAddStorage").visible)

        testCase.emptyCauseSeed = "emptyFolder"
        wait(20)
        compare(findChild(emptyState, "vaultBrowseEmptyHeading").text, "This folder is empty")
        compare(findChild(emptyState, "vaultBrowseEmptyBody").text,
                "Nothing here yet. Anything you drop in will appear on its own.")
        verify(!findChild(emptyState, "vaultBrowseEmptyAddStorage").visible)

        testCase.emptyCauseSeed = "allAway"
        testCase.emptyItemsCountSeed = 8
        wait(20)
        compare(findChild(emptyState, "vaultBrowseEmptyHeading").text, "Everything here is away")
        compare(findChild(emptyState, "vaultBrowseEmptyBody").text,
                "All 8 items live on a drive that is not connected. Nothing has been forgotten.")

        testCase.emptyCauseSeed = "filtered"
        testCase.emptyItemsCountSeed = 8
        wait(20)
        compare(findChild(emptyState, "vaultBrowseEmptyHeading").text, "Nothing matches that filter")
        compare(findChild(emptyState, "vaultBrowseEmptyBody").text,
                "Clear the filter to see all 8 items again.")

        // the whole point of the design contract (§4.5): no two causes share copy.
        var headings = ["No storage yet", "This folder is empty", "Everything here is away", "Nothing matches that filter"]
        var bodies = [
            "Point Vault at a folder or a drive and it will work out what is there.",
            "Nothing here yet. Anything you drop in will appear on its own.",
            "All 8 items live on a drive that is not connected. Nothing has been forgotten.",
            "Clear the filter to see all 8 items again."
        ]
        for (var i = 0; i < headings.length; ++i) {
            for (var j = i + 1; j < headings.length; ++j) {
                verify(headings[i] !== headings[j])
                verify(bodies[i] !== bodies[j])
            }
        }
    }

    // ── 15. Slice 9 — focus ring visible on keyboard focus, absent on hover/click ─────────────
    //
    // TRAP (caught by review, closing-gate slice): a GridView does not instantiate
    // `highlightItem` until something is current (`currentIndex >= 0`). `init()` leaves
    // `grid.currentIndex` at -1, and neither populating the grid nor a plain mouse click ever
    // touches it (production sets currentIndex only from `onActiveFocusChanged`, on keyboard
    // focus). So at the two points this test used to check, `grid.highlightItem` was null —
    // `verify(!grid.highlightItem || !grid.highlightItem.visible)` short-circuited on the first
    // clause and passed WITHOUT EVER READING `.visible`. Proven vacuous by mutation: setting the
    // highlight's `visible: true` in production (breaking design §4.9's keyboard-only contract
    // outright) still left all 196 cases green.
    //
    // Fix: give the GridView a current item WITHOUT granting keyboard focus (`currentIndex = 0`
    // directly — GridView creates `highlightItem` off `currentIndex`, independent of
    // `activeFocus`), so `highlightItem` genuinely EXISTS at the point its `visible` property is
    // read. Every check below asserts existence first, then the property — a null can never
    // stand in for "not visible" again.
    function test_focus_ring_visible_on_keyboard_focus_only() {
        testCase.levelData["/root"] = [
            { key: "/root/A", nodeType: "clip", displayTitle: "A", physicalFact: "local",
              path: "/root/A.mkv", counts: { items: 0 }, coverRef: "", state: "localOnly", away: false },
            { key: "/root/B", nodeType: "clip", displayTitle: "B", physicalFact: "local",
              path: "/root/B.mkv", counts: { items: 0 }, coverRef: "", state: "localOnly", away: false }
        ]
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)

        // Force a current item to exist (without focus) so `highlightItem` is real, not null.
        grid.currentIndex = 0
        wait(40)
        compare(grid.activeFocus, false)
        verify(grid.highlightItem !== null)
        compare(grid.highlightItem.visible, false)

        var firstArt = findChild(grid, "vaultBrowseCard_/root/A_art")
        verify(firstArt !== null)
        mouseClick(firstArt)
        wait(60)
        compare(grid.activeFocus, false)     // a mouse click never requests keyboard focus
        verify(grid.highlightItem !== null)
        compare(grid.highlightItem.visible, false)

        testWindow.requestActivate()
        grid.forceActiveFocus()
        wait(60)
        verify(grid.activeFocus)
        verify(grid.highlightItem !== null)
        compare(grid.highlightItem.visible, true)
    }

    // ── 16. Slice 9 — arrow traversal order matches the visual (model) order ──────────────────
    function test_arrow_traversal_matches_visual_order() {
        var rows = []
        for (var i = 0; i < 9; ++i) {
            rows.push({ key: "/root/N" + i, nodeType: "clip", displayTitle: "N" + i,
                physicalFact: "local", path: "/root/N" + i + ".mkv", counts: { items: 0 },
                coverRef: "", state: "localOnly", away: false })
        }
        testCase.levelData["/root"] = rows
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        compare(grid.count, 9)
        compare(testCase.browseGridWide, true)   // clip rows -> wide cards, cellWidth 320
        var columns = Math.floor(grid.width / grid.cellWidth)   // 700/320 -> 2
        verify(columns >= 2)

        testWindow.requestActivate()
        grid.forceActiveFocus()
        grid.currentIndex = 0
        wait(40)
        for (var k = 1; k < columns; ++k) {
            keyClick(Qt.Key_Right)
            wait(20)
            compare(grid.currentIndex, k)
        }
        var afterRow0 = grid.currentIndex
        keyClick(Qt.Key_Down)
        wait(20)
        compare(grid.currentIndex, afterRow0 + columns)     // one row down == +columns
        keyClick(Qt.Key_Left)
        wait(20)
        compare(grid.currentIndex, afterRow0 + columns - 1)
        keyClick(Qt.Key_Up)
        wait(20)
        compare(grid.currentIndex, afterRow0 - 1)
    }

    // ── 17. Slice 9 — Enter opens the keyboard-focused card; Backspace ascends ────────────────
    function test_enter_opens_focused_card_and_backspace_ascends() {
        testCase.levelData["/root"] = [
            { key: "/root/A", nodeType: "folder", displayTitle: "A", physicalFact: "2 items",
              path: "/root/A", counts: { items: 2 }, coverRef: "", state: "identified", away: false },
            { key: "/root/B", nodeType: "folder", displayTitle: "B", physicalFact: "1 item",
              path: "/root/B", counts: { items: 1 }, coverRef: "", state: "identified", away: false }
        ]
        testCase.levelData["/root/A"] = []
        testCase.selectRoot("/root", "hemanth's folder")
        wait(80)
        compare(crumb.stack.length, 1)

        testWindow.requestActivate()
        grid.forceActiveFocus()
        grid.currentIndex = 0
        wait(40)
        keyClick(Qt.Key_Return)
        wait(80)
        compare(testCase.currentBrowsePath, "/root/A")
        compare(crumb.stack.length, 2)

        keyClick(Qt.Key_Backspace)
        wait(80)
        compare(testCase.currentBrowsePath, "/root")
        compare(crumb.stack.length, 1)
    }

    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}

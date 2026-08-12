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

    property var crumbStack: []
    property string currentBrowsePath: ""
    property string lastOpenedPath: ""
    readonly property var browseGridRows: testCase.currentBrowsePath ? testCase.browseAt(testCase.currentBrowsePath) : []
    readonly property bool browseGridWide: testCase.browseGridRows.length > 0
        && (testCase.browseGridRows[0].nodeType === "episode" || testCase.browseGridRows[0].nodeType === "clip")

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
    function handleBrowseCardOpen(row) {
        if (!row) return
        if (row.nodeType === "folder" || row.nodeType === "show" || row.nodeType === "season") {
            testCase.pushCrumb(row.key, row.displayTitle)
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

    GridView {
        id: grid
        objectName: "vaultBrowseGrid"
        parent: testWindow.contentItem
        x: 300; y: 400
        width: 700; height: 320
        clip: true
        cellWidth: testCase.browseGridWide ? 320 : 170
        cellHeight: testCase.browseGridWide ? 250 : 300
        model: testCase.browseGridRows
        delegate: testCase.browseGridWide ? wideDelegateComp : posterDelegateComp
    }

    function init() {
        testCase.crumbStack = []
        testCase.currentBrowsePath = ""
        testCase.lastOpenedPath = ""
        testCase.levelData = ({})
        testCase.rootsSeed = []
        testCase.carouselSlidesSeed = []
        rail.expanded = false
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

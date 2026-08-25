import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault UX uplift S9 — the storage rail's downloads-chip + marquee-count wiring, driven with
// seeded rootsDetail()-shaped rows exactly the way VaultPage.qml feeds the production rail
// (roots + downloadsRootPath + rootFolderCount in, signals out; VaultPage owns the
// VaultLibrary calls, so this harness pins the component contract, not the façade — the C++
// half of "remove hides + republishes" is proved in tst_vault_forensics.cpp).
TestCase {
    id: testCase
    name: "VaultBrowseRailStorage"
    when: windowShown

    Window { id: testWindow; width: 720; height: 720; visible: true }

    property var rootsSeed: []
    property string downloadsSeed: "/downloads"
    property int countSeed: 0

    Colosseum.VaultBrowseRail {
        id: rail
        parent: testWindow.contentItem
        x: 20; y: 20
        height: 640
        expanded: true   // the marquee count line + remove affordance are expanded-only detail
        roots: testCase.rootsSeed
        downloadsRootPath: testCase.downloadsSeed
        rootFolderCount: testCase.countSeed
        onToggleRequested: rail.expanded = !rail.expanded
    }
    SignalSpy { id: removeSpy; signalName: "removeDownloadsRequested" }
    SignalSpy { id: selectedSpy; signalName: "rootSelected" }

    function init() {
        // Park the pointer off-target first (same hygiene as tst_vault_home_widget): a stale
        // hover from the previous case must not pre-reveal a hover-gated affordance.
        mouseMove(testWindow, testWindow.width - 1, testWindow.height - 1)
        testCase.rootsSeed = []
        testCase.downloadsSeed = "/downloads"
        testCase.countSeed = 0
        rail.expanded = true
        removeSpy.target = rail
        removeSpy.clear()
        selectedSpy.target = rail
        selectedSpy.clear()
        wait(20)
    }
    function cleanup() {
        removeSpy.target = null
        selectedSpy.target = null
    }

    function row(i) { return findChild(rail, "vaultBrowseRailRoot_" + i) }

    // The downloads chip, found by its own contract (isDownloads) rather than a name — this
    // ALSO pins the ordering: the flagged row must exist at a real index.
    function downloadsRow() {
        for (let i = 0; i < 8; ++i) {
            const r = row(i)
            if (r && r.isDownloads) return r
        }
        return null
    }

    // ── 1. the downloads chip renders MUTED and LAST, whatever order rootsDetail() returned ──
    // The synthetic root is seeded in the MIDDLE of the array on purpose: "always last" must
    // be the rail's own ordering (design decision 4), not an artifact of the seed.
    function test_downloads_chip_renders_muted_and_last() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 },
            { path: "/downloads", name: "Downloads", available: true, itemCount: 2, fileCount: 2 },
            { path: "/media/b", name: "Films", available: true, itemCount: 5, fileCount: 5 }
        ]
        wait(40)
        const downloads = downloadsRow()
        verify(downloads !== null)
        verify(downloads.isDownloads === true)
        verify(downloads.muted === true)
        // Always last: the downloads row IS the highest-index row, and the rows above it are
        // user roots (never muted).
        compare(row(0).rootPath, "/media/a")
        compare(row(1).rootPath, "/media/b")
        compare(row(2), downloads)
        verify(row(3) === null)
        verify(row(0).muted === false)
        verify(row(1).muted === false)
        // The muted treatment is real paint, not a flag: the muted row's glyph sits at the
        // away-glyph dimness (0.4) while an available user row's stays at 0.85, and its
        // name reads at the dimmer ink.
        compare(findChild(downloads, "vaultBrowseRailRootGlyph").opacity, 0.4)
        compare(findChild(row(0), "vaultBrowseRailRootGlyph").opacity, 0.85)
        verify(findChild(downloads, "vaultBrowseRailRootName").color
               !== findChild(row(0), "vaultBrowseRailRootName").color)
    }

    // ── 2. NEGATIVE CONTROL (falsifiability of test 1): with no synthetic root wired, no row
    //      is flagged as the downloads chip and nothing is muted. ────────────────────────────
    function test_no_downloads_root_wired_means_nothing_muted_or_reordered() {
        testCase.downloadsSeed = ""
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 },
            { path: "/media/b", name: "Films", available: true, itemCount: 5, fileCount: 5 }
        ]
        wait(40)
        verify(downloadsRow() === null)
        compare(row(0).rootPath, "/media/a")
        compare(row(1).rootPath, "/media/b")
        verify(row(0).muted === false)
        verify(row(1).muted === false)
    }

    // ── 3. the downloads chip's remove affordance fires removeDownloadsRequested ───────────
    //      (VaultPage wires that signal to VaultLibrary.removeDownloadsRoot(); the × only
    //      exists on the muted row, only on hover, and a plain row click must NOT remove.)
    function test_downloads_remove_affordance_fires_only_for_downloads_row() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 },
            { path: "/downloads", name: "Downloads", available: true, itemCount: 2, fileCount: 2 }
        ]
        wait(40)
        const downloads = downloadsRow()
        verify(downloads !== null)
        // Scoped to the downloads row: every delegate carries one remove Item (visible:false
        // on user rows), so a rail-wide findChild would return row 0's.
        const remove = findChild(downloads, "vaultBrowseRailDownloadsRemove")
        verify(remove !== null)
        verify(remove.visible === false)          // quiet: only on hover
        mouseMove(downloads, Math.min(40, downloads.width / 2), downloads.height / 2)
        tryVerify(function() { return rowMaHovered(downloads) })
        tryVerify(function() { return remove.visible === true })
        mouseClick(remove)
        compare(removeSpy.count, 1)
        // A user row click still selects; it never removes.
        mouseClick(row(0), row(0).width / 2, row(0).height / 2)
        compare(removeSpy.count, 1)
        compare(selectedSpy.count, 1)
        compare(selectedSpy.signalArguments[0][0], "/media/a")
    }

    // The row's own MouseArea, reached positionally (id rowMa lives inside the delegate).
    function rowMaHovered(rowItem) {
        const kids = rowItem.children || []
        for (let i = 0; i < kids.length; ++i)
            if (kids[i].hoverEnabled === true) return kids[i].containsMouse
        return false
    }

    // ── 4. the marquee count line states rootCount(), not the rail's row count ────────────
    //      rootFolderCount is its own property on purpose: the count VaultLibrary.rootCount()
    //      states can legitimately differ from roots.length (e.g. an unconfirmed root between
    //      census and confirm) — the line must read the marquee source, not the local array.
    function test_marquee_count_line_reads_root_count() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 },
            { path: "/media/b", name: "Films", available: true, itemCount: 5, fileCount: 5 }
        ]
        testCase.countSeed = 3                     // deliberately NOT roots.length (2)
        wait(40)
        const countText = findChild(rail, "vaultBrowseRailFolderCount")
        verify(countText !== null)
        verify(countText.visible === true)
        compare(countText.text, "· 3 folders")
        // singular is honest too
        testCase.countSeed = 1
        wait(40)
        compare(countText.text, "· 1 folder")
        // zero: the line is absent, not "· 0 folders"
        testCase.countSeed = 0
        wait(40)
        verify(countText.visible === false)
    }

    // ── 5. collapsed rail hides the marquee count line (§4.1: expanding reveals only detail) ──
    function test_marquee_count_is_expanded_only() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 }
        ]
        testCase.countSeed = 1
        rail.expanded = false
        wait(40)
        const countText = findChild(rail, "vaultBrowseRailFolderCount")
        verify(countText !== null)
        verify(countText.visible === false)
        rail.expanded = true
        wait(40)
        verify(countText.visible === true)
    }

    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted && wanted !== "") return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}

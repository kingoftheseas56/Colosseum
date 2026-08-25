import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault UX uplift S9 — the storage rail's downloads-chip + marquee-count wiring, driven with
// seeded rootsDetail()-shaped rows exactly the way VaultPage.qml feeds the production rail
// (roots + downloadsRootPath + rootFolderCount in, signals out; VaultPage owns the
// VaultLibrary calls, so this harness pins the component contract, not the façade — the C++
// half of "remove hides + republishes" is proved in tst_vault_forensics.cpp).
// Vault UX uplift S10 — the same rail's overflow menu (Rescan · Forget this storage… with
// its files-untouched confirm copy · the root's path) and the footer's ignore-patterns
// editor (seeded field, parsed Save, quiet Cancel), same seeded-signal contract.
TestCase {
    id: testCase
    name: "VaultBrowseRailStorage"
    when: windowShown

    Window { id: testWindow; width: 720; height: 720; visible: true }

    property var rootsSeed: []
    property string downloadsSeed: "/downloads"
    property int countSeed: 0
    property var ignoreSeed: []

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
    // S10
    SignalSpy { id: rescanSpy; signalName: "rescanRequested" }
    SignalSpy { id: forgetSpy; signalName: "forgetConfirmed" }
    SignalSpy { id: ignoreSpy; signalName: "scanIgnoreSaved" }

    function init() {
        // Park the pointer off-target first (same hygiene as tst_vault_home_widget): a stale
        // hover from the previous case must not pre-reveal a hover-gated affordance.
        mouseMove(testWindow, testWindow.width - 1, testWindow.height - 1)
        testCase.rootsSeed = []
        testCase.downloadsSeed = "/downloads"
        testCase.countSeed = 0
        testCase.ignoreSeed = []
        rail.expanded = true
        rail.scanIgnore = testCase.ignoreSeed
        removeSpy.target = rail
        removeSpy.clear()
        selectedSpy.target = rail
        selectedSpy.clear()
        rescanSpy.target = rail
        rescanSpy.clear()
        forgetSpy.target = rail
        forgetSpy.clear()
        ignoreSpy.target = rail
        ignoreSpy.clear()
        wait(20)
    }
    function cleanup() {
        removeSpy.target = null
        selectedSpy.target = null
        rescanSpy.target = null
        forgetSpy.target = null
        ignoreSpy.target = null
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

    // ── 6. S10: the row overflow menu — Rescan fires with the row's path; Forget arms a
    //      confirm whose copy states files on disk are untouched; Forget confirm fires;
    //      Cancel disarms without firing; the root's own path is stated, not clickable. ──
    function test_overflow_menu_rescan_and_forget_confirm_flow() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 }
        ]
        wait(40)
        const menu = findChild(rail, "vaultBrowseRailRowMenu")
        verify(menu !== null)
        verify(menu.visible === false)

        // open via the row's hover-revealed ⋮ handle (scoped: every row carries one)
        const overflow = findChild(row(0), "vaultBrowseRailRowOverflow")
        verify(overflow !== null)
        mouseMove(row(0), Math.min(30, row(0).width / 2), row(0).height / 2)
        tryVerify(function() { return overflow.visible === true })
        mouseClick(overflow)
        tryVerify(function() { return menu.visible === true })

        // the menu states the root's own path as its fact line
        const pathText = findChild(menu, "vaultBrowseRailMenuPath")
        verify(pathText !== null)
        compare(pathText.text, "/media/a")

        // Rescan fires with exactly the row's path and closes the menu
        mouseClick(findChild(menu, "vaultBrowseRailMenuRescan"))
        compare(rescanSpy.count, 1)
        compare(rescanSpy.signalArguments[0][0], "/media/a")
        tryVerify(function() { return menu.visible === false })

        // Forget arms the confirm; its copy MUST say files on disk are untouched
        mouseClick(overflow)
        tryVerify(function() { return menu.visible === true })
        mouseClick(findChild(menu, "vaultBrowseRailMenuForget"))
        const copy = findChild(menu, "vaultBrowseRailMenuCopy")
        verify(copy !== null)
        verify(copy.visible === true)
        verify(copy.text.indexOf("untouched") >= 0)
        compare(forgetSpy.count, 0)                 // arming alone never forgets

        // Cancel disarms back to the actions, still without forgetting
        mouseClick(findChild(menu, "vaultBrowseRailMenuCancel"))
        tryVerify(function() { return findChild(menu, "vaultBrowseRailMenuRescan").visible === true })
        compare(forgetSpy.count, 0)

        // The confirmed Forget fires with the row's path and closes the menu
        mouseClick(findChild(menu, "vaultBrowseRailMenuForget"))
        mouseClick(findChild(menu, "vaultBrowseRailMenuConfirm"))
        compare(forgetSpy.count, 1)
        compare(forgetSpy.signalArguments[0][0], "/media/a")
        tryVerify(function() { return menu.visible === false })
    }

    // ── 7. S10: the ignore-patterns editor — footer opens it seeded, Save parses the
    //      comma list into needles, Cancel saves nothing. ────────────────────────────────
    function test_ignore_editor_save_and_cancel() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9 }
        ]
        testCase.ignoreSeed = ["sample"]
        rail.scanIgnore = testCase.ignoreSeed
        wait(40)
        const editor = findChild(rail, "vaultBrowseRailIgnoreEditor")
        verify(editor !== null)
        verify(editor.visible === false)

        mouseClick(findChild(rail, "vaultBrowseRailIgnore"))
        tryVerify(function() { return editor.visible === true })
        const field = findChild(editor, "vaultBrowseRailIgnoreField")
        verify(field !== null)
        compare(field.text, "sample")               // seeded from the current needles

        field.text = "sample, extras;season pass,,  "   // messy input on purpose
        mouseClick(findChild(editor, "vaultBrowseRailIgnoreSave"))
        compare(ignoreSpy.count, 1)
        const needles = ignoreSpy.signalArguments[0][0]
        compare(needles.length, 3)
        compare(needles[0], "sample")
        compare(needles[1], "extras")
        compare(needles[2], "season pass")
        tryVerify(function() { return editor.visible === false })

        // Cancel: no second save, editor closed, nothing emitted
        mouseClick(findChild(rail, "vaultBrowseRailIgnore"))
        tryVerify(function() { return editor.visible === true })
        field.text = "never"
        mouseClick(findChild(editor, "vaultBrowseRailIgnoreCancel"))
        compare(ignoreSpy.count, 1)
        verify(editor.visible === false)
    }

    // ── 8. S11: the amber attention dot marks affected roots only, never a clean one. The
    //      rootsDetail() error facts arrive as modelData (errorCount/errorItems/watcherDegraded)
    //      exactly as VaultPage.qml feeds the rail. ───────────────────────────────────────
    function test_attention_dot_marks_affected_roots_only() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9,
              errorCount: 2,
              errorItems: [ { path: "/media/a/f0.mp4", reason: "corrupt" },
                            { path: "/media/a/f1.mkv", reason: "no video track" } ] },
            { path: "/media/b", name: "Films", available: true, itemCount: 5, fileCount: 5,
              watcherDegraded: true },
            { path: "/media/c", name: "Books", available: true, itemCount: 1, fileCount: 1 }
        ]
        wait(40)
        // error facts root → dot visible (per-row scoping; a rail-wide findChild would hit row 0)
        verify(row(0).hasNeedsAttention === true)
        verify(row(1).hasNeedsAttention === true)
        verify(row(2).hasNeedsAttention === false)
        var dot = findChild(row(0), "vaultBrowseRailRootErrorDot")
        verify(dot !== null)
        verify(dot.visible === true)
        dot = findChild(row(1), "vaultBrowseRailRootErrorDot")
        verify(dot !== null)
        verify(dot.visible === true)
        // NEGATIVE CONTROL: the clean root carries no dot at all
        dot = findChild(row(2), "vaultBrowseRailRootErrorDot")
        verify(dot !== null)
        verify(dot.visible === false)
    }

    // ── 9. S11: the overflow menu's attention line opens the plain "path · reason" list; the
    //      capped list says "+ N more" honestly; the watcher-degraded consequence is stated;
    //      Back returns to the actions; a clean root's menu shows no attention line at all. ──
    function test_attention_list_lists_reasons_and_stays_quiet_when_clean() {
        testCase.rootsSeed = [
            { path: "/media/a", name: "Archive", available: true, itemCount: 3, fileCount: 9,
              errorCount: 3,
              errorItems: [ { path: "/media/a/f0.mp4", reason: "corrupt" },
                            { path: "/media/a/f1.mkv", reason: "no video track" } ],
              watcherDegraded: true },
            { path: "/media/b", name: "Books", available: true, itemCount: 1, fileCount: 1 }
        ]
        wait(40)
        const menu = findChild(rail, "vaultBrowseRailRowMenu")

        // open row 0's menu: the count line is present and honest about the total
        mouseMove(row(0), Math.min(30, row(0).width / 2), row(0).height / 2)
        mouseClick(findChild(row(0), "vaultBrowseRailRowOverflow"))
        tryVerify(function() { return menu.visible === true })
        const attLine = findChild(menu, "vaultBrowseRailMenuAttention")
        verify(attLine !== null)
        verify(attLine.lineVisible === true)           // the row is armed, not just present
        verify(findText(menu, "3 files need attention…") !== null)

        // Clicking the count line arms the attention panel (the line's footprint is constant
        // by design — see the line's own comment — so its slot cannot drift) and the list
        // opens with both capped items.
        mouseClick(attLine)
        tryVerify(function() { return findChild(menu, "vaultBrowseRailAttentionTitle").visible === true })
        const item0 = findChild(menu, "vaultBrowseRailAttentionItem_0")
        const item1 = findChild(menu, "vaultBrowseRailAttentionItem_1")
        verify(item0 !== null); verify(item1 !== null)
        verify(findText(item0, "/media/a/f0.mp4") !== null)
        verify(findText(item0, "corrupt") !== null)
        verify(findText(item1, "no video track") !== null)
        verify(findChild(menu, "vaultBrowseRailAttentionMore").visible === true)
        verify(findText(menu, "+ 1 more") !== null)
        const note = findChild(menu, "vaultBrowseRailAttentionWatcherNote")
        verify(note !== null)
        verify(note.visible === true)
        verify(note.text.indexOf("rescanned") >= 0)

        // Back returns to the actions (the cancel-not-close shape)
        const back = findChild(menu, "vaultBrowseRailAttentionBack")
        mouseClick(back)
        tryVerify(function() { return findChild(menu, "vaultBrowseRailMenuRescan").visible === true })
        verify(rail.attentionArmed === false)
        verify(findChild(menu, "vaultBrowseRailAttentionTitle").visible === false)
        tryVerify(function() { return menu.visible === true })
        mouseMove(rail, rail.width / 2, rail.height - 1)   // park the pointer off the row
        wait(20)

        // NEGATIVE CONTROL: the clean root's menu offers no attention line at all. The row-0
        // menu is still up (its panel covers row 1, so row 1's overflow can never hover) —
        // close it via the panel's click-away first, then open the clean row's menu.
        mouseClick(rail, 5, rail.height - 30)
        tryVerify(function() { return menu.visible === false })
        mouseMove(row(1), Math.min(30, row(1).width / 2), row(1).height / 2)
        mouseClick(findChild(row(1), "vaultBrowseRailRowOverflow"))
        tryVerify(function() { return menu.visible === true })
        verify(findChild(menu, "vaultBrowseRailMenuAttention").lineVisible === false)
        verify(findText(menu, "needs attention…") === null)
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
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

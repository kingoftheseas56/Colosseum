import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault execution Slice 13 — the file-first folder view. Drives the PRODUCTION VaultFolderView with
// a seeded row model (no backend) and proves: both panes render from the seed, the fileCount /
// groupCount contract, the real filename is visible in each row, a sort-mode switch reorders the
// list (first-row title per mode), and Reveal emits its signal with the folder path.
TestCase {
    name: "VaultFolderView"
    when: windowShown

    Window { id: testWindow; width: 1100; height: 760; visible: true }

    Component { id: viewComp; Colosseum.VaultFolderView {} }
    property var view: null

    SignalSpy { id: revealSpy; signalName: "revealRequested" }

    // Two loose volumes + one Extras file. Natural order = seed order (Vol 2, Vol 10, then Extras).
    readonly property var seed: [
        { "id": "a", "path": "D:/Show/Vol 2.cbz",  "displayTitle": "Vol 2",  "realName": "Vol 2.cbz",
          "subfolder": "", "kind": "comic", "size": 100, "mtimeMs": 2000, "pages": 200, "progressed": false, "coverUrl": "" },
        { "id": "b", "path": "D:/Show/Vol 10.cbz", "displayTitle": "Vol 10", "realName": "Vol 10.cbz",
          "subfolder": "", "kind": "comic", "size": 100, "mtimeMs": 1000, "pages": 200, "progressed": false, "coverUrl": "" },
        { "id": "c", "path": "D:/Show/Extras/Color 1.cbz", "displayTitle": "Color 1", "realName": "Color 1.cbz",
          "subfolder": "Extras", "kind": "comic", "size": 100, "mtimeMs": 3000, "pages": 96, "progressed": false, "coverUrl": "" }
    ]

    function init() {
        view = viewComp.createObject(testWindow, {
            model: seed, title: "Show", kind: "comic", rootPath: "D:/Show"
        })
        verify(view !== null)
        revealSpy.target = view
        wait(40)
    }
    function cleanup() {
        revealSpy.clear(); revealSpy.target = null
        if (view) view.destroy()
        view = null
    }

    function test_seeded_model_renders_both_panes() {
        compare(view.fileCount, 3)
        compare(view.groupCount, 1)                       // one non-empty subfolder: Extras
        verify(findChild(view, "vaultFileRow_0") !== null)
        verify(findChild(view, "vaultFileRow_2") !== null)
        verify(findChild(view, "vaultFolderContinue") !== null)
        verify(findChild(view, "vaultFolderReveal") !== null)
    }

    function test_row_exposes_clean_title_and_real_filename() {
        var r0 = findChild(view, "vaultFileRow_0")
        verify(r0 !== null)
        compare(r0.displayTitle, "Vol 2")
        compare(r0.realName, "Vol 2.cbz")                 // the real filename rides in the row
    }

    function test_natural_order_is_seed_order() {
        compare(view.sortMode, "natural")
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Vol 2")
    }

    function test_alpha_sort_reorders_first_row() {
        view.sortMode = "alpha"
        wait(40)
        // "Color 1" < "Vol 10" < "Vol 2" case-insensitive → Color 1 leads
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Color 1")
    }

    function test_newest_sort_reorders_first_row() {
        view.sortMode = "newest"
        wait(40)
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Color 1") // mtime 3000 leads
    }

    function test_reveal_emits_folder_path() {
        mouseClick(findChild(view, "vaultFolderReveal"))
        compare(revealSpy.count, 1)
        compare(revealSpy.signalArguments[0][0], "D:/Show")
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}

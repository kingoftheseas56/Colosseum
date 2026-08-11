import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum
import "../../qml/VaultApi.js" as VaultApi

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
    SignalSpy { id: worldSpy; signalName: "viewWorldRequested" }

    // Two loose volumes + one Extras file. The root projection shows two direct files and one folder;
    // entering Extras shows its one direct file while the backing model remains three rows.
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
        worldSpy.target = view
        wait(40)
    }
    function cleanup() {
        revealSpy.clear(); revealSpy.target = null
        worldSpy.clear(); worldSpy.target = null
        if (view) view.destroy()
        view = null
    }

    function test_seeded_model_renders_both_panes() {
        compare(view.fileCount, 3)
        compare(view.groupCount, 1)                       // one non-empty subfolder: Extras
        compare(view.visibleFileCount, 2)
        verify(findChild(view, "vaultFileRow_0") !== null)
        verify(findChild(view, "vaultFileRow_1") !== null)
        verify(findChild(view, "vaultFolderRow_Extras") !== null)
        verify(findChild(view, "vaultFileRow_2") === null)
        verify(findChild(view, "vaultFolderContinue") !== null)
        verify(findChild(view, "vaultFolderReveal") !== null)
        verify(!view.worldDoorReady)
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
        // The folder remains a folder; direct root files sort independently.
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Vol 10")
    }

    function test_newest_sort_reorders_first_row() {
        view.sortMode = "newest"
        wait(40)
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Vol 2")
    }

    function test_one_level_folder_projection_and_descent() {
        var folder = findChild(view, "vaultFolderRow_Extras")
        verify(folder !== null)
        compare(folder.folderPath, "Extras")
        compare(folder.descendantCount, 1)

        view.currentFolder = folder.folderPath
        wait(40)
        compare(view.currentFolder, "Extras")
        compare(view.fileCount, 3)
        compare(view.visibleFileCount, 1)
        compare(findChild(view, "vaultFileRow_0").displayTitle, "Color 1")
        verify(findChild(view, "vaultFolderUp") !== null)

        view.currentFolder = ""
        wait(40)
        compare(view.currentFolder, "")
        compare(view.visibleFileCount, 2)
    }

    function test_progress_is_tri_state_and_live_joined() {
        view.model = [
            { "id": "none", "path": "D:/Show/None.cbz", "displayTitle": "None", "realName": "None.cbz",
              "subfolder": "", "kind": "comic", "size": 1, "mtimeMs": 0, "pages": 1,
              "progressed": false, "hasProgress": false, "coverUrl": "" },
            { "id": "partial", "path": "D:/Show/Partial.cbz", "displayTitle": "Partial", "realName": "Partial.cbz",
              "subfolder": "", "kind": "comic", "size": 1, "mtimeMs": 0, "pages": 1,
              "progressed": false, "hasProgress": true, "progressFraction": 0.42,
              "progressFinished": false, "coverUrl": "" },
            { "id": "done", "path": "D:/Show/Done.cbz", "displayTitle": "Done", "realName": "Done.cbz",
              "subfolder": "", "kind": "comic", "size": 1, "mtimeMs": 0, "pages": 1,
              "progressed": true, "hasProgress": true, "progressFraction": 0.9,
              "progressFinished": true, "coverUrl": "" }
        ]
        wait(40)
        compare(view.fileCount, 3)
        compare(findChild(view, "vaultFileRow_0").progressLabel, "–")
        compare(findChild(view, "vaultFileRow_1").progressLabel, "42%")
        compare(findChild(view, "vaultFileRow_2").progressLabel, "✓")
    }

    function test_reveal_emits_folder_path() {
        mouseClick(findChild(view, "vaultFolderReveal"))
        compare(revealSpy.count, 1)
        compare(revealSpy.signalArguments[0][0], "D:/Show")
    }

    function test_identity_enables_world_door_without_changing_file_model() {
        view.identityId = "mal:3"
        view.identitySource = "MAL"
        view.identityWorld = "Tankoban"
        view.synopsis = "A seeded synopsis."
        view.synopsisSource = "MAL"
        wait(30)
        verify(view.hasSynopsis)
        var door = findChild(view, "vaultFolderViewWorld")
        verify(door !== null)
        verify(view.worldDoorReady)
        mouseClick(door)
        compare(worldSpy.count, 1)
        compare(worldSpy.signalArguments[0][0].identityId, "mal:3")
        compare(view.fileCount, 3)
    }

    function test_identified_synopsis_is_source_named_and_pane_bounded() {
        view.synopsis = "A seeded synopsis."
        view.synopsisSource = "MAL"
        wait(30)

        var synopsis = findChild(view, "vaultFolderSynopsis")
        verify(synopsis !== null)
        verify(synopsis.visible)
        compare(synopsis.text, "A seeded synopsis.")
        var source = findChild(view, "vaultFolderSynopsisSource")
        verify(source !== null)
        verify(source.visible)
        compare(source.text, "Source: MAL")

        var paneViewport = findChild(view, "vaultFolderPaneViewport")
        verify(paneViewport !== null)
        verify(paneViewport.clip)
        verify(paneViewport.y + paneViewport.height <= view.height)
    }

    function test_unidentified_model_hides_synopsis_block() {
        view.synopsis = ""
        view.synopsisSource = ""
        wait(30)
        verify(!findChild(view, "vaultFolderSynopsis").visible)
        verify(!findChild(view, "vaultFolderSynopsisSource").visible)
    }

    function test_live_progress_join_carries_completion_without_index_field() {
        var progress = {
            get: function(kind, id) {
                return id === "video-finished"
                    ? { id: id, progress: 0.9, watched: true }
                    : { id: id, progress: 1.0, resume: { finished: true } }
            }
        }
        var finishedVideo = VaultApi.joinRow(progress, {
            id: "video-finished", kind: "video", progressed: false
        })
        var finishedComic = VaultApi.joinRow(progress, {
            id: "comic-finished", kind: "comic", progressed: false
        })
        compare(finishedVideo.progressFraction, 0.9)
        compare(finishedVideo.hasProgress, true)
        compare(finishedVideo.progressFinished, true)
        compare(finishedComic.progressFraction, 1.0)
        compare(finishedComic.progressFinished, true)
        verify(finishedVideo.progressFraction !== undefined)
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

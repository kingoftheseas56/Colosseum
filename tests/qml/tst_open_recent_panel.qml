import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault execution Slice 9 — the Open Recent panel. Drives the PRODUCTION OpenRecentPanel with a
// seeded model and proves: a seeded model renders one row per entry, clicking an available row
// emits reopenRequested carrying that entry, a DEAD (unavailable) row offers nothing, and Clear
// emits clearRequested — after which an emptied model shows zero rows.
TestCase {
    name: "OpenRecentPanel"
    when: windowShown

    Window { id: testWindow; width: 400; height: 400; visible: true }

    Component { id: panelComp; Colosseum.OpenRecentPanel {} }
    property var panel: null

    SignalSpy { id: reopenSpy; signalName: "reopenRequested" }
    SignalSpy { id: clearSpy; signalName: "clearRequested" }

    readonly property var seed: [
        { "path": "D:/lib/one.cbz", "title": "One", "kind": "comic", "vaultId": "vault:1", "available": true },
        { "path": "D:/lib/two.mp4", "title": "Two", "kind": "video", "vaultId": "vault:2", "available": false }
    ]

    function init() {
        panel = panelComp.createObject(testWindow, { model: seed })
        verify(panel !== null)
        reopenSpy.target = panel
        clearSpy.target = panel
        wait(40)
    }
    function cleanup() {
        reopenSpy.clear(); clearSpy.clear()
        reopenSpy.target = null; clearSpy.target = null
        if (panel) panel.destroy()
        panel = null
    }

    function test_seeded_model_renders_one_row_per_entry() {
        compare(panel.rowCount, 2)
        verify(findChild(panel, "openRecentRow_0") !== null)
        verify(findChild(panel, "openRecentRow_1") !== null)
        verify(findChild(panel, "openRecentClear") !== null)
    }

    function test_available_row_click_emits_reopen_with_entry() {
        var row0 = findChild(panel, "openRecentRow_0")
        verify(row0 !== null)
        mouseClick(row0)
        compare(reopenSpy.count, 1)
        compare(reopenSpy.signalArguments[0][0].path, "D:/lib/one.cbz")
    }

    function test_dead_row_offers_nothing() {
        var row1 = findChild(panel, "openRecentRow_1") // available == false
        verify(row1 !== null)
        mouseClick(row1)
        compare(reopenSpy.count, 0)
    }

    function test_clear_requests_and_emptied_model_shows_zero_rows() {
        var clear = findChild(panel, "openRecentClear")
        verify(clear !== null)
        mouseClick(clear)
        compare(clearSpy.count, 1)
        // the host wipes the store and re-seeds the model; simulate the emptied model
        panel.model = []
        compare(panel.rowCount, 0)
        verify(findChild(panel, "openRecentRow_0") === null)
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

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "NextToOpenTray"
    when: windowShown

    Window { id: testWindow; width: 620; height: 500; visible: true }
    Component { id: trayComp; Colosseum.NextToOpenTray {} }
    property var tray: null
    SignalSpy { id: openSpy; signalName: "openRequested" }
    SignalSpy { id: removeSpy; signalName: "removeRequested" }

    readonly property var seed: [
        { path: "D:/media/one.cbz", title: "One", family: "comic", accepted: true },
        { path: "D:/media/two.mp4", title: "Two", family: "video", accepted: true }
    ]

    function init() {
        tray = trayComp.createObject(testWindow, { model: seed })
        verify(tray !== null)
        openSpy.target = tray
        removeSpy.target = tray
        wait(40)
    }
    function cleanup() {
        openSpy.target = null; removeSpy.target = null
        if (tray) tray.destroy()
        tray = null
    }

    function test_seeded_tray_renders_exact_count_and_rows() {
        compare(tray.stagedCount, 2)
        verify(findChild(tray, "nextToOpenTray") !== null)
        verify(findChild(tray, "nextToOpenRow_0") !== null)
        verify(findChild(tray, "nextToOpenRow_1") !== null)
    }

    function test_row_open_emits_index_and_entry() {
        var row = findChild(tray, "nextToOpenRow_0")
        verify(row !== null)
        mouseClick(row)
        compare(openSpy.count, 1)
        compare(openSpy.signalArguments[0][0], 0)
        compare(openSpy.signalArguments[0][1].path, "D:/media/one.cbz")
    }

    function test_remove_emits_and_model_count_can_drop() {
        var remove = findChild(tray, "nextToOpenRemove_1")
        verify(remove !== null)
        mouseClick(remove)
        compare(removeSpy.count, 1)
        compare(removeSpy.signalArguments[0][0], 1)
        tray.model = [seed[0]]
        compare(tray.stagedCount, 1)
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

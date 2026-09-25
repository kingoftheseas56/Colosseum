import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../../qml" as Colosseum

TestCase {
    name: "TrackerSyncCenterTaskbar"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: barComponent; Colosseum.Taskbar {} }
    property var bar: null
    SignalSpy { id: syncSpy; signalName: "syncCenterClicked" }

    function init() {
        bar = barComponent.createObject(testWindow, {
            "width": testWindow.width,
            "height": testWindow.height,
            "open": true
        })
        verify(bar !== null)
        syncSpy.target = bar
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true, 1000)
        wait(320)
    }

    function cleanup() {
        syncSpy.clear()
        syncSpy.target = null
        if (bar)
            bar.destroy()
        bar = null
    }

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findChild(kids[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function test_sync_sits_after_keyboard_guide_before_sessions() {
        var guide = findChild(bar, "taskbarKeyboardGuide")
        var sync = findChild(bar, "taskbarSyncCenter")
        var sessions = findChild(bar, "taskbarSessionTiles")
        verify(guide !== null)
        verify(sync !== null)
        verify(sessions !== null)
        verify(guide.x < sync.x)
        verify(sync.x < sessions.x)
        compare(sync.visible, true)

        bar.open = false
        wait(300)
        compare(sync.visible, false)
    }

    function test_existing_dock_order_is_preserved() {
        var orderedNames = [
            "taskbarOpenMedia", "openRecentDisclosure", "taskbarWatchPartyJoin",
            "taskbarDownloads", "taskbarExtensions", "taskbarSettings",
            "taskbarKeyboardGuide", "taskbarSyncCenter", "taskbarSessionTiles"
        ]
        var previousX = -1
        for (var i = 0; i < orderedNames.length; ++i) {
            var item = findChild(bar, orderedNames[i])
            verify(item !== null, orderedNames[i] + " is present")
            var x = item.mapToItem(bar, 0, 0).x
            verify(x > previousX, orderedNames[i] + " remains in dock order")
            previousX = x
        }
    }

    function test_sync_is_an_accessible_intent_and_active_destination() {
        var sync = findChild(bar, "taskbarSyncCenter")
        var surface = findChild(bar, "taskbarSyncCenterSurface")
        var icon = findChild(bar, "taskbarSyncCenterIcon")
        var action = findChild(sync, "taskbarSyncCenterInput")
        verify(sync !== null)
        verify(surface !== null)
        verify(icon !== null)
        verify(action !== null)
        compare(icon.status, Image.Ready)
        compare(action.Accessible.name, "Connections")
        compare(bar.syncAttentionCount, 0)
        compare(syncSpy.count, 0)
        mouseClick(sync)
        compare(syncSpy.count, 1)

        action.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(action, "activeFocus", true, 1000)
        keyClick(Qt.Key_Return)
        compare(syncSpy.count, 2)
        keyClick(Qt.Key_Enter)
        compare(syncSpy.count, 3)
        keyClick(Qt.Key_Space)
        compare(syncSpy.count, 4)

        bar.syncCenterActive = true
        wait(0)
        compare(findChild(sync, "taskbarSyncCenter").visible, true)
    }

    function test_return_focus_reopens_taskbar_and_targets_sync() {
        var sync = findChild(bar, "taskbarSyncCenter")
        var action = findChild(sync, "taskbarSyncCenterInput")
        verify(sync !== null)
        verify(action !== null)
        bar.reveal()
        compare(bar.autoRevealed, true)
        bar.open = false
        wait(0)
        compare(sync.visible, false)

        bar.focusSyncCenterAction()
        tryCompare(bar, "open", true, 1000)
        compare(bar.autoRevealed, false)
        tryCompare(action, "activeFocus", true, 1000)
    }
}

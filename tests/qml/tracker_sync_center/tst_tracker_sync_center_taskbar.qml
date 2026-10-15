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
    Component { id: topBarComponent; Colosseum.TopBar {} }
    Item { id: backdropStub; width: 1280; height: 720 }
    property var bar: null
    property var topBar: null
    SignalSpy { id: syncSpy; signalName: "trackersClicked" }
    SignalSpy { id: historySpy; signalName: "historyStatsClicked" }

    function init() {
        bar = barComponent.createObject(testWindow, {
            "width": testWindow.width,
            "height": testWindow.height,
            "open": true
        })
        verify(bar !== null)
        topBar = topBarComponent.createObject(testWindow, {
            "backdrop": backdropStub,
            "width": testWindow.width,
            "height": 80,
            "trackersEnabled": true
        })
        verify(topBar !== null)
        syncSpy.target = topBar
        historySpy.target = bar
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true, 1000)
        wait(320)
    }

    function cleanup() {
        syncSpy.clear()
        syncSpy.target = null
        historySpy.clear()
        historySpy.target = null
        if (bar)
            bar.destroy()
        bar = null
        if (topBar)
            topBar.destroy()
        topBar = null
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

    function test_keyboard_guide_and_sync_left_the_dock() {
        compare(findChild(bar, "taskbarKeyboardGuide"), null)
        compare(findChild(bar, "taskbarSyncCenter"), null)
    }

    function test_existing_dock_order_is_preserved() {
        var orderedNames = [
            "taskbarOpenMedia", "openRecentDisclosure", "taskbarWatchPartyJoin",
            "taskbarDownloads", "taskbarExtensions", "taskbarHistoryStats",
            "taskbarSessionTiles"
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

    function test_history_is_an_accessible_intent_and_active_destination() {
        var history = findChild(bar, "taskbarHistoryStats")
        var action = findChild(history, "taskbarHistoryStatsInput")
        verify(history !== null)
        verify(action !== null)
        compare(action.Accessible.name, "History, highlights, and stats")
        compare(historySpy.count, 0)
        mouseClick(history)
        compare(historySpy.count, 1)

        action.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(action, "activeFocus", true, 1000)
        keyClick(Qt.Key_Return)
        compare(historySpy.count, 2)

        bar.historyStatsActive = true
        wait(0)
        compare(history.visible, true)
    }

    function test_trackers_door_is_an_accessible_top_bar_intent() {
        var door = findChild(topBar, "topBarTrackersButton")
        var action = findChild(topBar, "topBarTrackersInput")
        verify(door !== null)
        verify(action !== null)
        compare(door.visible, true)
        compare(action.Accessible.name, "Trackers")
        compare(syncSpy.count, 0)
        mouseClick(door)
        compare(syncSpy.count, 1)

        topBar.focusTrackersButton()
        tryCompare(action, "activeFocus", true, 1000)
        keyClick(Qt.Key_Return)
        compare(syncSpy.count, 2)
    }

    function test_trackers_door_is_opt_in_per_host() {
        var door = findChild(topBar, "topBarTrackersButton")
        verify(door !== null)
        topBar.trackersEnabled = false
        wait(0)
        compare(door.visible, false)
    }

    function test_trackers_door_shows_no_status_without_connections() {
        compare(topBar.trackersDotState, "")
        compare(findChild(topBar, "topBarTrackersStatusDot").visible, false)
    }
}

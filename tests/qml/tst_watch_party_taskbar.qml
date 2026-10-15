// Watch Party Slice 6 — outside-player join-door signal seam.
//
// Drives the production Taskbar and proves that Join Watch Party is an expanded-dock action,
// not a hidden Player-only path. The test stops at the signal boundary; Main owns opening the
// Join sheet and no room/network operation is performed here.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "WatchPartyTaskbar"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: barComponent; Colosseum.Taskbar {} }
    property var bar: null

    SignalSpy { id: joinSpy; signalName: "watchPartyJoinClicked" }

    function init() {
        bar = barComponent.createObject(
            testWindow,
            { "width": 1280, "height": 720, "open": true })
        verify(bar !== null)
        joinSpy.target = bar
        wait(320)
    }

    function cleanup() {
        joinSpy.clear()
        joinSpy.target = null
        if (bar)
            bar.destroy()
        bar = null
    }

    function test_join_door_is_expanded_taskbar_action() {
        var control = findChild(bar, "taskbarWatchPartyJoin")
        verify(control !== null)
        verify(control.visible)

        bar.open = false
        wait(30)
        verify(!control.visible)

        bar.open = true
        wait(320)
        verify(control.visible)
    }

    function test_click_emits_join_request_once() {
        var control = findChild(bar, "taskbarWatchPartyJoin")
        verify(control !== null)
        compare(joinSpy.count, 0)
        mouseClick(control)
        compare(joinSpy.count, 1)
    }

    function test_slice06_join_sheet_open_marks_taskbar_action_active() {
        var control = findChild(bar, "taskbarWatchPartyJoin")
        var surface = findChild(bar, "taskbarWatchPartyJoinSurface")
        var glyph = findChild(bar, "taskbarWatchPartyJoinGlyph")
        verify(control !== null)
        verify(surface !== null)
        verify(glyph !== null)

        bar.watchPartyJoinOpen = true
        wait(0)
        compare(control.atlasActive, true)
        compare(surface.activeState, true)
        compare(glyph.activeState, true)
    }

    function test_slice06_join_sheet_closed_clears_taskbar_action_active() {
        var control = findChild(bar, "taskbarWatchPartyJoin")
        var surface = findChild(bar, "taskbarWatchPartyJoinSurface")
        verify(control !== null)
        verify(surface !== null)

        bar.watchPartyJoinOpen = true
        wait(0)
        compare(control.atlasActive, true)
        bar.watchPartyJoinOpen = false
        wait(0)
        compare(control.atlasActive, false)
        compare(surface.activeState, false)
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
}

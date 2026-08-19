// Slice 07 RED/GREEN visual-integration contract.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3

TestCase {
    name: "WatchPartyVisualHarness"
    when: windowShown

    Window {
        id: testWindow
        width: 1440
        height: 900
        visible: true
    }

    Component {
        id: harnessComponent
        WatchPartyVisualHarness { }
    }

    property var harness: null

    function init() {
        harness = harnessComponent.createObject(testWindow.contentItem, { "width": 1440, "height": 900 })
        verify(harness !== null)
        harness.showPanel = true
        harness.applyState("hostControl")
        wait(0)
    }

    function cleanup() {
        if (harness)
            harness.destroy()
        harness = null
    }

    function child(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var hit = child(kids[i], objectName)
            if (hit)
                return hit
        }
        return null
    }

    function test_slice07_normal_player_geometry() {
        var pop = child(harness, "watchPartyPanel")
        verify(pop !== null)
        compare(Math.round(pop.width), 430)
        verify(pop.height <= 590)
        verify(pop.x >= 10)
        verify(pop.y >= 10)
        verify(pop.x + pop.width <= harness.width - 10 + 0.5)
    }

    function test_slice07_narrow_player_geometry_stays_bounded() {
        harness.width = 560
        harness.height = 480
        wait(0)
        var pop = child(harness, "watchPartyPanel")
        var control = child(harness, "watchPartyPlayerControl")
        verify(pop.width <= harness.width - 20 + 0.5)
        verify(pop.x >= 10)
        verify(pop.x + pop.width <= harness.width - 10 + 0.5)
        verify(pop.y >= 10)
        var controlTop = control.mapToItem(harness, 0, 0).y
        verify(pop.y + pop.height <= controlTop - 10 + 0.5)
    }

    function test_slice07_arrow_tracks_watch_party_control() {
        var arrow = child(harness, "watchPartyPanelArrow")
        var control = child(harness, "watchPartyPlayerControl")
        verify(arrow !== null && control !== null)
        var controlPoint = control.mapToItem(harness, control.width / 2, control.height / 2)
        compare(Math.round(arrow.x + arrow.width / 2), Math.round(controlPoint.x))
    }

    function test_slice07_all_in_player_atlas_states() {
        var states = ["hostControl", "hostShared", "participant", "sourceNotReady",
                      "catchUp", "reconnecting", "hostGrace"]
        for (var i = 0; i < states.length; ++i) {
            harness.applyState(states[i])
            wait(0)
            compare(harness.visualState, states[i])
            verify(child(harness, "watchPartyPanel").visible)
        }
    }

    function test_slice07_overflow_fixture_scrolls_without_growth() {
        harness.applyOverflowFixture()
        wait(0)
        var pop = child(harness, "watchPartyPanel")
        var body = child(harness, "watchPartyBody")
        var thumb = child(harness, "watchPartyScrollThumb")
        var chat = child(harness, "watchPartyChatFlick")
        verify(pop.height <= 590)
        verify(body.contentHeight > body.height)
        verify(thumb.visible)
        verify(chat.contentHeight > chat.height)
    }

    function test_slice07_escape_closes_and_returns_focus() {
        var focusScope = child(harness, "watchPartyPanelFocus")
        var control = child(harness, "watchPartyPlayerControl")
        verify(focusScope !== null && control !== null)
        testWindow.requestActivate()
        focusScope.forceActiveFocus()
        wait(40)
        verify(focusScope.activeFocus)
        keyClick(Qt.Key_Escape)
        wait(60)
        compare(harness.watchPartyPanel.panelOpen, false)
        verify(control.activeFocus)
    }
}

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "KeyboardRegion"
    when: windowShown

    Window { id: testWindow; width: 420; height: 240; visible: true }

    Component {
        id: regionComp
        Colosseum.KeyboardRegion {
            id: testRegion
            width: 280
            height: 150
            entryItem: firstAction
            trapTab: true
            property alias firstAction: firstAction
            property alias secondAction: secondAction

            Colosseum.KeyboardAction {
                id: firstAction
                x: 10
                y: 10
                width: 100
                height: 40
                pointerEnabled: false
                accessibleName: "First"
            }
            Colosseum.KeyboardAction {
                id: secondAction
                x: 10
                y: 60
                width: 100
                height: 40
                pointerEnabled: false
                accessibleName: "Second"
            }
        }
    }

    Component {
        id: commandActionComp
        Item {
            width: 120
            height: 60
            Colosseum.KeyboardCommand {
                id: command
                semanticId: "test.action"
                label: "Test action"
            }
            Colosseum.KeyboardAction {
                id: action
                anchors.fill: parent
                pointerEnabled: false
                command: command
                accessibleName: "Test action"
            }
            property alias commandObject: command
            property alias actionObject: action
        }
    }

    property var region: null
    property var commandAction: null
    SignalSpy { id: escapeSpy; signalName: "escapeRequested" }
    SignalSpy { id: commandSpy; signalName: "triggered" }

    function init() {
        region = regionComp.createObject(testWindow)
        verify(region !== null)
        escapeSpy.target = region
        testWindow.requestActivate()
        wait(40)
    }

    function cleanup() {
        escapeSpy.clear()
        commandSpy.clear()
        escapeSpy.target = null
        commandSpy.target = null
        if (region) region.destroy()
        if (commandAction) commandAction.destroy()
        region = null
        commandAction = null
    }

    function test_entry_focus_chooses_first_valid_control() {
        compare(region.focusableItems().length, 2)
        verify(region.focusEntry())
        verify(region.firstAction.activeFocus)
    }

    function test_tab_and_backtab_stay_inside_trapped_region() {
        region.firstAction.forceActiveFocus()
        wait(5)
        verify(region.firstAction.activeFocus)
        verify(region.handleKey(Qt.Key_Tab, Qt.NoModifier))
        verify(region.secondAction.activeFocus)
        verify(region.handleKey(Qt.Key_Backtab, Qt.NoModifier))
        verify(region.firstAction.activeFocus)
        compare(escapeSpy.count, 0)
    }

    function test_real_arrow_events_follow_visible_vertical_geometry() {
        region.firstAction.forceActiveFocus(Qt.OtherFocusReason)
        wait(5)
        keyClick(Qt.Key_Down)
        verify(region.secondAction.activeFocus)
        keyClick(Qt.Key_Up)
        verify(region.firstAction.activeFocus)
    }

    function test_escape_emits_one_region_request() {
        verify(region.focusEntry())
        verify(region.handleKey(Qt.Key_Escape, Qt.NoModifier))
        compare(escapeSpy.count, 1)
    }

    function test_last_focus_restores_only_while_valid() {
        region.secondAction.forceActiveFocus()
        wait(5)
        verify(region.secondAction.activeFocus)
        verify(region.rememberFocus(region.secondAction))
        verify(region.restoreFocus())
        verify(region.secondAction.activeFocus)
        region.secondAction.visible = false
        verify(region.restoreFocus())
        verify(region.firstAction.activeFocus)
    }

    function test_disabled_controls_are_not_focus_targets() {
        region.firstAction.enabled = false
        verify(region.focusEntry())
        verify(region.secondAction.activeFocus)
        region.secondAction.enabled = false
        verify(!region.focusEntry())
    }

    function test_keyboard_action_invokes_registered_command() {
        commandAction = commandActionComp.createObject(testWindow)
        verify(commandAction !== null)
        commandSpy.target = commandAction.commandObject
        commandAction.actionObject.activate(Qt.TabFocusReason)
        compare(commandSpy.count, 1)
        compare(commandSpy.signalArguments[0][0], commandAction.actionObject)
    }

    function test_restore_rejects_zero_opacity_ancestor_even_when_child_flags_stay_true() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as Colosseum; Item { width: 180; height: 120; visible: true; opacity: 0;'
            + 'property alias target: target; Colosseum.KeyboardAction { id: target; width: 100; height: 40; visible: true; enabled: true; focusEnabled: true; pointerEnabled: false; property string stableId: "hidden-parent" } }',
            region)
        region.returnFocusItem = null
        region.returnSnapshot = { item: fixture.target, identity: "hidden-parent", scrolls: [] }
        fixture.opacity = 0
        compare(fixture.target.visible, true)
        compare(fixture.target.enabled, true)
        verify(!region._validInternal(fixture.target))
        region.restoreFocus()
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }

    function test_restore_rejects_mostly_clipped_center_inside_target() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as Colosseum; Item { width: 180; height: 120; clip: true;'
            + 'property alias target: target; Colosseum.KeyboardAction { id: target; x: 10; y: 70; width: 100; height: 80; visible: true; enabled: true; focusEnabled: true; pointerEnabled: false; property string stableId: "clipped-parent" } }',
            region)
        region.returnFocusItem = null
        region.returnSnapshot = { item: fixture.target, identity: "clipped-parent", scrolls: [] }
        verify(!region._validInternal(fixture.target))
        region.restoreFocus()
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }

    function test_restore_rejects_target_covered_by_closing_overlay() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as Colosseum; Item { width: 180; height: 120;'
            + 'property alias target: target; Colosseum.KeyboardAction { id: target; x: 20; y: 20; width: 100; height: 40; visible: true; enabled: true; focusEnabled: true; pointerEnabled: false; property string stableId: "covered-parent" }'
            + 'Rectangle { objectName: "closingOverlay"; anchors.fill: parent; color: "black"; z: 10; visible: true; opacity: 1 } }',
            region)
        region.returnFocusItem = null
        region.returnSnapshot = { item: fixture.target, identity: "covered-parent", scrolls: [] }
        verify(!region._validInternal(fixture.target))
        region.restoreFocus()
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }
}

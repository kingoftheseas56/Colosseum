import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../qml" as App
import "../../qml/account" as Account
import "../../qml/collections" as Collections

TestCase {
    id: testCase
    name: "AndroidTooltipFoundation"
    when: windowShown
    Window {
        id: testWindow
        width: 620; height: 540; visible: true
        Item {
            id: fixture
            anchors.fill: parent
            property int playCalls: 0
            property int removeCalls: 0
            property int busyCalls: 0

            function resetRows() {
                overflow.actions = [
                    { id: "play", label: "Play next", iconName: "plus", enabled: true, busy: false, selected: true },
                    { id: "remove", label: "Remove", iconName: "trash", enabled: false, busy: false, selected: false },
                    { id: "busy", label: "Download", iconName: "down", enabled: true, busy: true, selected: false }
                ]
            }

            App.KeyboardAction {
                id: action
                x: 18; y: 28
                width: 140; height: 40
                accessibleName: "Open details"
                tooltipText: "Open details"
                androidPresentationEnabled: true
            }
            SignalSpy { id: actionTriggers; target: action; signalName: "triggered" }
            App.ActionOverflowMenu {
                id: overflow
                x: 360; y: 48
                showLabel: true
                onActionTriggered: function(actionId) {
                    if (actionId === "play") fixture.playCalls++
                    if (actionId === "remove") fixture.removeCalls++
                    if (actionId === "busy") fixture.busyCalls++
                }
            }
            Account.AccountField {
                id: passwordField
                x: 18; y: 170; width: 340
                label: "Password"
                hint: "At least eight characters"
                password: true
                controlObjectName: "tooltipTestPassword"
                androidPresentationEnabled: true
            }
            Collections.CollectionActionButton {
                id: collectionAction
                x: 18; y: 310
                iconName: "up"
                tooltipText: "Move earlier"
                compact: true
                androidPresentationEnabled: true
            }
        }
    }

    function tap(item) {
        const p = item.mapToItem(testWindow.contentItem, item.width / 2, item.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(25)
    }
    function init() {
        testWindow.requestActivate()
        fixture.playCalls = 0
        fixture.removeCalls = 0
        fixture.busyCalls = 0
        fixture.resetRows()
        overflow.x = 360
        actionTriggers.clear()
        overflow.close()
        action._longPressTooltip = false
        action.androidPresentationEnabled = true
        action.androidTooltipPolicy = "legacy"
        action.androidCaptionText = ""
        action.androidCaptionPlacement = "below"
        action.tooltipHovered = false
        passwordField.androidPresentationEnabled = true
        passwordField.helperText = ""
        collectionAction.androidTooltipPolicy = "auto"
    }
    function cleanup() { overflow.close() }

    function test_keyboard_legacy_caption_drop_and_long_press() {
        compare(action.androidTooltipPolicy, "legacy")
        compare(action.androidCaptionVisible, false)
        action.tooltipHovered = true
        compare(action.tooltipVisibilityRequested, true) // legacy retains current hover path
        action.androidTooltipPolicy = "caption"
        action.androidCaptionText = "Details"
        compare(action.androidEffectiveCaption, "Details")
        compare(action.androidCaptionVisible, true)
        compare(action.tooltipVisibilityRequested, false)
        const caption = findChild(action, "androidActionCaption")
        verify(caption !== null)
        compare(caption.text, "Details")
        action.androidTooltipPolicy = "drop"
        compare(action.androidCaptionVisible, false)
        compare(action.tooltipVisibilityRequested, false)
        action.androidTooltipPolicy = "longPress"
        compare(action.androidCaptionVisible, false)
        compare(action.tooltipVisibilityRequested, false)
        action._longPressTooltip = true
        compare(action.tooltipVisibilityRequested, true)
        action._longPressTooltip = false
        compare(action.tooltipVisibilityRequested, false)
    }
    function test_long_press_gesture_shows_fallback_without_activation() {
        action.androidTooltipPolicy = "longPress"
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1100)
        verify(action._longPressTooltip, "Touch press-and-hold should request the Qt ToolTip")
        compare(action.tooltipVisibilityRequested, true)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 0)
    }
    function test_desktop_path_ignores_android_policy() {
        action.androidPresentationEnabled = false
        action.androidTooltipPolicy = "caption"
        action.tooltipHovered = true
        compare(action.androidCaptionVisible, false)
        compare(action.ToolTip.text, "Open details")
        compare(action.tooltipVisibilityRequested, true)
        action.androidTooltipPolicy = "drop"
        compare(action.tooltipVisibilityRequested, true)
    }
    function test_tap_opens_menu_and_row_calls_original_handler() {
        compare(overflow.opened, false)
        tap(overflow)
        tryVerify(function() { return overflow.opened })
        const play = overflow.rowAt(0)
        verify(play !== null)
        compare(play.caption, "Play next")
        compare(play.selected, true)
        tap(play)
        tryCompare(fixture, "playCalls", 1)
        compare(overflow.opened, false)
    }
    function test_disabled_and_busy_rows_do_not_fire() {
        tap(overflow)
        tryVerify(function() { return overflow.opened })
        const disabledRow = overflow.rowAt(1)
        const busyRow = overflow.rowAt(2)
        compare(disabledRow.available, false)
        compare(busyRow.busy, true)
        tap(disabledRow)
        tap(busyRow)
        compare(fixture.removeCalls, 0)
        compare(fixture.busyCalls, 0)
    }
    function test_popup_stays_inside_window_near_both_edges() {
        overflow.x = 10
        overflow.open()
        tryVerify(function() { return overflow.opened })
        let row = overflow.rowAt(0)
        let left = row.mapToItem(testWindow.contentItem, 0, 0).x
        verify(left >= 0, "Left-edge More popup must remain onscreen")
        overflow.close()
        overflow.x = testWindow.width - overflow.width - 8
        overflow.open()
        row = overflow.rowAt(0)
        left = row.mapToItem(testWindow.contentItem, 0, 0).x
        verify(left + row.width <= testWindow.width, "Right-edge More popup must remain onscreen")
    }
    function test_model_reassignment_updates_live_row_state() {
        overflow.actions = [
            { id: "play", label: "Play next", iconName: "plus", enabled: true, busy: true, selected: false }
        ]
        const row = overflow.rowAt(0)
        verify(row !== null)
        compare(row.busy, true)
        compare(row.selected, false)
        overflow.actions = [
            { id: "play", label: "Play now", iconName: "plus", enabled: true, busy: false, selected: true }
        ]
        const newRow = overflow.rowAt(0)
        compare(newRow.caption, "Play now")
        compare(newRow.busy, false)
        compare(newRow.selected, true)
    }
    function test_android_helper_below_field_and_desktop_above() {
        const help = findChild(passwordField, "accountFieldHelperText")
        const input = findChild(passwordField, "tooltipTestPassword")
        verify(help !== null && input !== null)
        compare(help.text, "At least eight characters")
        verify(help.y >= input.y + input.height)
        passwordField.helperText = "This stays visible"
        compare(help.text, "This stays visible")
        passwordField.androidPresentationEnabled = false
        const desktopHint = findChild(passwordField, "accountFieldDesktopHintText")
        verify(desktopHint !== null)
        compare(desktopHint.text, "At least eight characters")
        verify(desktopHint.visible)
        verify(desktopHint.y < input.y)
    }
    function test_collection_icon_only_uses_visible_android_copy() {
        compare(collectionAction._visibleLabel, "Move earlier")
        collectionAction.androidTooltipPolicy = "drop"
        compare(collectionAction._visibleLabel, "")
        collectionAction.androidTooltipPolicy = "caption"
        collectionAction.androidCaptionText = "Move up"
        compare(collectionAction._visibleLabel, "Move up")
    }
}

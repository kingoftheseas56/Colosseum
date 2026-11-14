import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../qml" as App

// Android has no hover: KeyboardAction shows its existing tooltip on press-and-hold.
// Desktop keeps the hover tooltip. No layout or label changes on either platform.
TestCase {
    id: testCase
    name: "AndroidLongPressTooltip"
    when: windowShown
    Window {
        id: testWindow
        width: 400; height: 240; visible: true
        App.KeyboardAction {
            id: action
            x: 18; y: 28
            width: 140; height: 40
            accessibleName: "Open details"
            tooltipText: "Open details"
            longPressTooltipEnabled: true
        }
        SignalSpy { id: actionTriggers; target: action; signalName: "triggered" }
    }

    function init() {
        testWindow.requestActivate()
        actionTriggers.clear()
        action._longPressTooltip = false
        action.longPressTooltipEnabled = true
        action.tooltipHovered = false
    }

    function test_android_hover_alone_shows_nothing() {
        action.tooltipHovered = true
        compare(action.tooltipRequested, false)
    }
    function test_android_long_press_shows_tooltip_without_activation() {
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1100)
        verify(action._longPressTooltip, "press-and-hold should request the tooltip")
        compare(action.tooltipRequested, true)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 0)
    }
    function test_android_short_tap_still_activates() {
        wait(700)
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 1)
        compare(action._longPressTooltip, false)
    }
    function test_desktop_keeps_hover_tooltip() {
        action.longPressTooltipEnabled = false
        action.tooltipHovered = true
        compare(action.ToolTip.text, "Open details")
        compare(action.tooltipRequested, true)
    }
}

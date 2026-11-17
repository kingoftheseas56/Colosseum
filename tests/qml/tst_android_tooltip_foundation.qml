import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../qml" as App
import "../../qml/reader2" as Reader2

// Android has no hover: KeyboardAction shows its existing tooltip on press-and-hold.
// Desktop keeps the hover tooltip. No layout or label changes on either platform.
TestCase {
    id: testCase
    name: "AndroidLongPressTooltip"
    when: windowShown
    Window {
        id: testWindow
        width: 1280; height: 720; visible: true
        App.TooltipStyle {
            id: tooltipStyle
            backdrop: testWindow.contentItem
        }
        Reader2.ReaderChrome {
            id: readerChrome
            anchors.fill: parent
            title: "Test book"
            author: "Test author"
            chapterLabel: "Chapter 1"
            shellWindowed: true
        }
        App.KeyboardAction {
            id: action
            x: 18; y: 500
            width: 140; height: 40
            accessibleName: "Open details"
            tooltipText: "Open details"
            longPressTooltipEnabled: true
        }
        QtObject {
            id: sourcePicker
            property string smartPlayQuality: "off"
            function setSmartPickEnabled(enabled) {}
        }
        App.SmartPickToggle {
            id: smartPick
            x: 216; y: 180
            visible: true
            sourcePicker: sourcePicker
        }
        SignalSpy { id: actionTriggers; target: action; signalName: "triggered" }
    }
    function init() {
        testWindow.requestActivate()
        mouseMove(testWindow.contentItem, 5, 300)
        actionTriggers.clear()
        action._longPressTooltip = false
        action.longPressTooltipEnabled = true
        action.tooltipHovered = false
        readerChrome.closeAnyPanel()
        readerChrome.revealState = ({ shown: false, lastActive: 0, pinned: false, frozen: false })
        readerChrome.awake = false
        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
    }

    function test_android_hover_alone_shows_nothing() {
        action.tooltipHovered = true
        compare(action.tooltipRequested, false)
    }
    function test_android_long_press_shows_tooltip_without_activation() {
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(600)
        verify(action._longPressTooltip, "the hint should appear ~0.4 s into the hold, finger still down")
        compare(action.tooltipRequested, true)
        compare(action.ToolTip.delay, 0)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 0)
        compare(action.tooltipRequested, true)   // stays up after release, hides ~1.5 s later
        wait(1700)
        compare(action.tooltipRequested, false)
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

    function test_attached_tooltip_uses_colosseum_style() {
        const tip = action.ToolTip.toolTip
        verify(tip !== null)
        compare(tip.padding, 0)
        compare(tip.leftPadding, 16)
        compare(tip.rightPadding, 16)
        verify(tip.contentItem !== null)
    }

    function test_smart_pick_tooltip_uses_colosseum_style() {
        const tip = findChild(smartPick, "smartPickHoverExplanation")
        verify(tip !== null)
        compare(tip.width, 280)
        compare(tip.padding, 0)
        compare(tip.leftPadding, 16)
        compare(tip.rightPadding, 16)
        verify(tip.contentItem !== null)
    }

    function findTooltipArea(root, label) {
        if (!root) return null
        try {
            if (root.tooltipText === label && root.containsMouse !== undefined)
                return root
        } catch (error) {}
        const descendants = root.children || []
        for (let i = 0; i < descendants.length; ++i) {
            const found = findTooltipArea(descendants[i], label)
            if (found) return found
        }
        return null
    }

    function findReaderTopBar(area) {
        let item = area
        while (item) {
            try {
                if (item.shown !== undefined && item.shellWindowed !== undefined)
                    return item
            } catch (error) {}
            item = item.parent
        }
        return null
    }

    function test_reader_chrome_appearance_tooltip_reveals_without_activation() {
        const area = findTooltipArea(readerChrome, "Appearance")
        verify(area !== null)
        const topBar = findReaderTopBar(area)
        verify(topBar !== null)
        const tip = area.ToolTip.toolTip
        verify(tip !== null)
        compare(readerChrome.appearanceOpen, false)

        const directChildren = readerChrome.children || []
        const topBands = []
        for (let i = 0; i < directChildren.length; ++i) {
            const child = directChildren[i]
            if (Math.abs(child.width - readerChrome.width) < 1
                    && Math.abs(child.height - readerChrome.revealBandPx) < 1
                    && Math.abs(child.y) < 1)
                topBands.push(child)
        }
        compare(topBands.length, 1)
        const topBand = topBands[0]
        compare(topBar.parent, topBand)
        let revealHandler = null
        const bandData = topBands[0].data || []
        for (let i = 0; i < bandData.length; ++i) {
            try {
                if (bandData[i].hovered !== undefined && bandData[i].blocking !== undefined) {
                    revealHandler = bandData[i]
                    break
                }
            } catch (error) {}
        }
        verify(revealHandler !== null)
        compare(revealHandler.blocking, false)

        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
        const hiddenCenter = area.mapToItem(testWindow.contentItem, area.width / 2, area.height / 2)
        mouseMove(testWindow.contentItem, hiddenCenter.x, readerChrome.revealBandPx + 4)
        mouseMove(testWindow.contentItem, hiddenCenter.x, readerChrome.revealBandPx - 6)
        tryCompare(readerChrome, "awake", true)
        tryVerify(function() { return topBar.opacity > 0.99 })
        const center = area.mapToItem(testWindow.contentItem, area.width / 2, area.height / 2)
        mouseMove(testWindow.contentItem, center.x, center.y)
        tryCompare(revealHandler, "hovered", true)
        tryCompare(area, "containsMouse", true)
        tryVerify(function() { return tip.visible && tip.text === "Appearance" })
        compare(readerChrome.appearanceOpen, false)
        verify(tip.background !== null)
        const panelOrigin = tip.background.mapToItem(testWindow.contentItem, 0, 0)
        verify(panelOrigin.x >= 0 && panelOrigin.y >= 0
               && panelOrigin.x + tip.width <= testWindow.width
               && panelOrigin.y + tip.height <= testWindow.height)
        verify(readerChrome.revealState.frozen)

        mouseClick(area, area.width / 2, area.height / 2)
        tryCompare(readerChrome, "appearanceOpen", true)
        verify(readerChrome.awake)
        readerChrome.closeAppearance(false)
        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
        tryCompare(revealHandler, "hovered", false)
        tryVerify(function() { return !readerChrome.revealState.frozen })
        tryCompare(readerChrome, "awake", false, 4000)
    }

    function check_smart_pick_tooltip_at(controlX, expectedPanelX) {
        smartPick.x = controlX
        const info = findChild(smartPick, "smartPickInfo")
        const tip = findChild(smartPick, "smartPickHoverExplanation")
        verify(info !== null)
        verify(tip !== null)

        mouseMove(testWindow.contentItem, 5, 300)
        tryCompare(tip, "visible", false)
        mouseMove(info, info.width / 2, info.height / 2)
        tryCompare(info, "hovered", true)
        tryCompare(tip, "visible", true)
        tryCompare(tip, "width", 280)
        tryCompare(tip, "margins", 12)

        const panel = tip.background
        verify(panel !== null)
        tryVerify(function() { return tip.parent === info && panel.anchorItem === info })
        const notch = findChild(panel, "tooltipPointerNotch")
        verify(notch !== null)
        tryCompare(notch, "visible", true)
        const iconOrigin = info.mapToItem(testWindow.contentItem, 0, 0)
        const expectedX = Math.max(12, Math.min(testWindow.width - tip.width - 12,
                                                iconOrigin.x + (info.width - tip.width) / 2))
        const expectedY = Math.max(12, Math.min(testWindow.height - tip.height - 12,
                                                iconOrigin.y + info.height + 8))
        const actualPanelOrigin = panel.mapToItem(testWindow.contentItem, 0, 0)
        tryVerify(function() { return Math.abs(expectedX - expectedPanelX) < 1 })
        tryVerify(function() {
            const panelOrigin = panel.mapToItem(testWindow.contentItem, 0, 0)
            return Math.abs(panelOrigin.x - expectedX) < 1
                && Math.abs(panelOrigin.y - expectedY) < 1
                && panelOrigin.x >= 12 && panelOrigin.y >= 12
                && panelOrigin.x + tip.width <= testWindow.width - 12
                && panelOrigin.y + tip.height <= testWindow.height - 12
        })
        tryVerify(function() {
            const anchorCentre = info.mapToItem(panel, info.width / 2, info.height / 2)
            const expectedNotchX = Math.max(panel.radius + 7,
                                            Math.min(panel.width - panel.radius - 7, anchorCentre.x))
            return Math.abs(panel.notchX - expectedNotchX) < 1
                && Math.abs(notch.x + notch.width / 2 - panel.notchX) < 1
        })
    }

    function test_smart_pick_tooltip_stays_centered_in_window() {
        check_smart_pick_tooltip_at(94, 184)
    }

    function test_smart_pick_tooltip_clamps_at_left_edge() {
        check_smart_pick_tooltip_at(-180, 12)
    }

    function test_smart_pick_tooltip_clamps_at_right_edge() {
        check_smart_pick_tooltip_at(1024, 988)
    }
}

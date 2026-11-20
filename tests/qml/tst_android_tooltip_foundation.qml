import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../qml" as App
import "../../qml/reader2" as Reader2

TestCase {
    id: testCase
    name: "KeyboardActionWithoutTooltips"
    when: windowShown
    Window {
        id: testWindow
        width: 1280; height: 720; visible: true
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
        }
        SignalSpy { id: actionTriggers; target: action; signalName: "triggered" }
    }
    function init() {
        testWindow.requestActivate()
        mouseMove(testWindow.contentItem, 5, 300)
        actionTriggers.clear()
        readerChrome.closeAnyPanel()
        readerChrome.revealState = ({ shown: false, lastActive: 0, pinned: false, frozen: false })
        readerChrome.awake = false
        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
    }

    function test_touch_hold_does_not_show_a_tooltip() {
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(600)
        compare(action.ToolTip.visible, false)
        compare(actionTriggers.count, 0)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        compare(action.ToolTip.visible, false)
    }

    function test_short_tap_still_activates() {
        wait(700)
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 1)
        compare(action.ToolTip.visible, false)
    }
    function findKeyboardArea(root, label) {
        if (!root) return null
        try {
            if (root.keyboardLabel === label && root.containsMouse !== undefined)
                return root
        } catch (error) {}
        const descendants = root.children || []
        for (let i = 0; i < descendants.length; ++i) {
            const found = findKeyboardArea(descendants[i], label)
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

    function test_reader_chrome_reveals_and_appearance_remains_clickable() {
        const area = findKeyboardArea(readerChrome, "Appearance")
        verify(area !== null)
        const topBar = findReaderTopBar(area)
        verify(topBar !== null)
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
        wait(450)
        compare(area.ToolTip.visible, false)
        compare(readerChrome.appearanceOpen, false)
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

}

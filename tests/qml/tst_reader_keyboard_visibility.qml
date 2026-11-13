import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/reader2" as R

TestCase {
    id: testCase
    name: "ReaderKeyboardVisibility"
    when: windowShown
    Window {
        id: win
        width: 1100; height: 760; visible: true
        Item { id: paper; activeFocusOnTab: true; width: 100; height: 100 }
        R.ReaderChrome { id: chrome; anchors.fill: parent; pagesInChapter: 10 }
        Item { id: after; activeFocusOnTab: true; width: 100; height: 100 }
    }
    SignalSpy { id: appearanceSpy; target: chrome; signalName: "appearanceEdited" }
    function controls(root, out) {
        out = out || []
        if (root.keyboardLabel !== undefined) out.push(root)
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) controls(kids[i], out)
        return out
    }
    function action(label) {
        var all = controls(chrome)
        for (var i = 0; i < all.length; ++i)
            if (all[i].keyboardLabel === label) return all[i]
        return null
    }
    function effectivelyEnabled(item) {
        for (var p = item; p; p = p.parent) if (!p.enabled) return false
        return true
    }
    function visiblyOnscreen(item) {
        for (var p = item; p; p = p.parent)
            if (!p.visible || p.opacity < 0.99) return false
        var pos = item.mapToItem(win.contentItem, 0, 0)
        return pos.x >= 0 && pos.y >= 0 && pos.x + item.width <= win.width
            && pos.y + item.height <= win.height
    }
    function asleep() {
        chrome.closeAnyPanel()
        paper.forceActiveFocus()
        chrome.revealState = ({shown: false, lastActive: 0, pinned: false, frozen: false})
        chrome.awake = false
        wait(350)
    }
    function init() { win.requestActivate(); asleep() }
    function test_closed_panel_controls_are_not_tabbable() {
        var contents = action("Bookmarks")
        var paperTheme = action("Paper")
        verify(contents !== null && paperTheme !== null)
        verify(!effectivelyEnabled(contents), "Closed Bookmarks panel must be disabled")
        verify(!effectivelyEnabled(paperTheme), "Closed Appearance panel must be disabled")
    }
    function test_tab_reveals_and_pins_toolbar() {
        keyClick(Qt.Key_Tab)
        tryCompare(chrome, "awake", true)
        compare(win.activeFocusItem.keyboardLabel, "Back")
        wait(3500)
        compare(chrome.awake, true)
        compare(chrome.revealState.pinned, true)
        compare(win.activeFocusItem.keyboardLabel, "Back")
        // Leaving the reader's controls releases the pin and restores idle hiding.
        paper.forceActiveFocus()
        tryCompare(chrome, "awake", false, 4200)
    }
    function test_tab_can_exit_both_directions() {
        keyClick(Qt.Key_Tab)
        tryCompare(chrome, "awake", true)
        var reachedAfter = false
        for (var i = 0; i < 30; ++i) {
            keyClick(Qt.Key_Tab)
            if (after.activeFocus) { reachedAfter = true; break }
            wait(350)
            verify(effectivelyEnabled(win.activeFocusItem))
            verify(visiblyOnscreen(win.activeFocusItem), "Every focused control must be visible onscreen")
        }
        verify(reachedAfter, "Tab must leave the reader toolbar")
        keyClick(Qt.Key_Backtab)
        var reachedPaper = false
        for (var j = 0; j < 30; ++j) {
            keyClick(Qt.Key_Backtab)
            if (paper.activeFocus) { reachedPaper = true; break }
        }
        verify(reachedPaper, "Shift+Tab must leave the reader toolbar")
    }
    function test_reverse_tab_reveals_last_control() {
        after.forceActiveFocus()
        keyClick(Qt.Key_Backtab)
        tryCompare(chrome, "awake", true)
        compare(win.activeFocusItem.keyboardLabel, "Book progress")
    }
    function test_appearance_is_visible_before_keyboard_activation() {
        for (var i = 0; i < 2; ++i) {
            chrome.handleAppearance()
            tryCompare(chrome, "appearanceOpen", true)
            wait(350)
            compare(win.activeFocusItem.keyboardLabel, "Paper")
            verify(visiblyOnscreen(win.activeFocusItem))
            appearanceSpy.clear()
            keyClick(Qt.Key_Return)
            compare(appearanceSpy.count, 1)
            compare(appearanceSpy.signalArguments[0][0], "theme")
            compare(appearanceSpy.signalArguments[0][1], "paper")
            keyClick(Qt.Key_Escape)
            tryCompare(chrome, "appearanceOpen", false)
            tryVerify(function() { return win.activeFocusItem.keyboardLabel === "Appearance" })
            verify(!effectivelyEnabled(action("Paper")))
        }
    }
    function test_panel_escape_restores_visible_toolbar_focus() {
        chrome.wake()
        chrome.handleContents()
        tryCompare(chrome, "panelOpen", true)
        wait(350)
        keyClick(Qt.Key_Escape)
        tryCompare(chrome, "panelOpen", false)
        tryVerify(function() { return win.activeFocusItem.keyboardLabel === "Contents" && chrome.awake })
        wait(3500)
        compare(chrome.awake, true)
    }
}

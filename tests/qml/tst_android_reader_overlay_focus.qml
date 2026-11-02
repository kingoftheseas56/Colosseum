import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/reader2" as Reader

// Exercise the real popovers across the same surface/host parent boundary used by
// ReaderOverlay. Two desktop QQuickWindows do not qualify Android native stacking.
TestCase {
    id: testCase
    name: "AndroidReaderOverlayFocus"
    when: windowShown

    Window {
        id: hostWindow
        width: 960; height: 640; visible: true
        property alias keyboard: hostKeys
        TestCase { id: hostKeys; name: ""; when: false; optional: true }
    }
    Component {
        id: overlayWindowComponent
        Window {
            width: 960; height: 640; visible: true
            property alias keyboard: overlayKeys
            TestCase { id: overlayKeys; name: ""; when: false; optional: true }
        }
    }
    Component {
        id: footnoteComponent
        Reader.FootnoteCard { width: 960; height: 640; text: "A footnote." }
    }
    Component {
        id: dictionaryComponent
        Reader.DictCard { width: 960; height: 640; word: "reading"; dictState: "loading" }
    }
    SignalSpy { id: dismissed; signalName: "dismissed" }
    SignalSpy { id: external; signalName: "openExternal" }

    function popup(factory) {
        const item = createTemporaryObject(factory, hostWindow.contentItem)
        verify(item !== null)
        dismissed.target = item; dismissed.clear()
        return item
    }
    function makeWindow() {
        const item = createTemporaryObject(overlayWindowComponent, testCase)
        verify(item !== null)
        return item
    }
    function showInWindow(card, overlay) {
        card.surface.parent = overlay.contentItem
        overlay.visible = true
        overlay.requestActivate()
        card.shown = true
        tryCompare(overlay, "activeFocusItem", card.surface)
    }

    function test_footnote_native_surface_receives_tab_and_escape() {
        const card = popup(footnoteComponent)
        const overlay = makeWindow()
        const originalParent = card.surface.parent
        try {
            showInWindow(card, overlay)
            overlay.keyboard.keyClick(Qt.Key_Tab)
            compare(overlay.activeFocusItem, card.surface)
            overlay.keyboard.keyClick(Qt.Key_Escape)
            tryCompare(dismissed, "count", 1)
        } finally {
            card.surface.parent = originalParent
        }
    }
    function test_dictionary_native_focus_cycles_and_resets_on_reopen() {
        const card = popup(dictionaryComponent)
        const link = findChild(card, "reader2DictExternal")
        verify(link !== null)
        external.target = card; external.clear()
        const overlay = makeWindow()
        const originalParent = card.surface.parent
        try {
            showInWindow(card, overlay)
            // Loading has no link: Tab must stay inside this native surface.
            overlay.keyboard.keyClick(Qt.Key_Tab)
            compare(overlay.activeFocusItem, card.surface)
            card.dictState = "empty"
            overlay.keyboard.keyClick(Qt.Key_Tab)
            tryCompare(link, "activeFocus", true)
            card.shown = false
            overlay.visible = false
            wait(20)
            overlay.visible = true
            overlay.requestActivate()
            card.shown = true
            tryCompare(overlay, "activeFocusItem", card.surface,
                       5000, "reopening must reset focus even after a child owned it")
            overlay.keyboard.keyClick(Qt.Key_Tab)
            tryCompare(link, "activeFocus", true)
            overlay.keyboard.keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
            tryCompare(overlay, "activeFocusItem", card.surface)
            overlay.keyboard.keyClick(Qt.Key_Escape)
            tryCompare(dismissed, "count", 1)
            compare(external.count, 0)
        } finally {
            card.surface.parent = originalParent
        }
    }
    function test_desktop_focus_target_is_preserved_data() {
        return [{ tag: "footnote", factory: footnoteComponent },
                { tag: "dictionary", factory: dictionaryComponent }]
    }
    function test_desktop_focus_target_is_preserved(data) {
        const card = popup(data.factory)
        hostWindow.requestActivate()
        card.shown = true
        tryCompare(hostWindow, "activeFocusItem", card)
        hostWindow.keyboard.keyClick(Qt.Key_Escape)
        tryCompare(dismissed, "count", 1)
    }
}

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
    Component {
        id: appearanceComponent
        Reader.AppearancePanel { width: 960; height: 640 }
    }
    SignalSpy { id: dismissed; signalName: "dismissed" }
    SignalSpy { id: external; signalName: "openExternal" }
    SignalSpy { id: appearanceClosed; signalName: "closeRequested" }
    SignalSpy { id: appearanceChanged; signalName: "changed" }

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
    function test_appearance_focus_and_settings_data() {
        return [{ tag: "desktop", reparent: false },
                { tag: "native_surface", reparent: true }]
    }
    function test_appearance_focus_and_settings(data) {
        const panel = createTemporaryObject(appearanceComponent, hostWindow.contentItem)
        verify(panel !== null)
        const overlay = data.reparent ? makeWindow() : hostWindow
        const originalParent = panel.surface.parent
        appearanceClosed.target = panel; appearanceClosed.clear()
        appearanceChanged.target = panel; appearanceChanged.clear()
        try {
            if (data.reparent) panel.surface.parent = overlay.contentItem
            overlay.requestActivate()
            panel.open = true
            tryVerify(function() { return overlay.activeFocusItem
                                         && overlay.activeFocusItem.keyboardLabel === "Paper" })
            verify(panel.focusBelongsHere(overlay.activeFocusItem))
            overlay.keyboard.keyClick(Qt.Key_Return)
            tryCompare(appearanceChanged, "count", 1)
            compare(appearanceChanged.signalArguments[0][0], "theme")
            compare(appearanceChanged.signalArguments[0][1], "paper")
            overlay.keyboard.keyClick(Qt.Key_Tab)
            tryCompare(overlay.activeFocusItem, "keyboardLabel", "Sepia")
            overlay.keyboard.keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
            tryCompare(overlay.activeFocusItem, "keyboardLabel", "Paper")
            overlay.keyboard.keyClick(Qt.Key_Escape)
            tryCompare(appearanceClosed, "count", 1)
            panel.open = false
            compare(panel.overlayActive, false)
            panel.open = true
            tryVerify(function() { return overlay.activeFocusItem
                                         && overlay.activeFocusItem.keyboardLabel === "Paper" })
            mouseClick(panel.surface, 10, 100)
            tryCompare(appearanceClosed, "count", 2)
        } finally {
            panel.surface.parent = originalParent
        }
    }
    function test_appearance_uses_android_native_overlay() {
        const panel = createTemporaryObject(appearanceComponent, hostWindow.contentItem)
        verify(panel !== null)
        compare(panel.nativeWindow, true)
        compare(panel.nativeOverlay, Qt.platform.os === "android")
    }
}

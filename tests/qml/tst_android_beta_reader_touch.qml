import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/reader2" as Reader

// Actual chrome components with a deterministic IME rectangle. These checks do not
// exercise Android's foreign WebView/native-window stacking or its real keyboard.
TestCase {
    id: testCase
    name: "AndroidBetaReaderTouch"
    when: windowShown

    Window { id: testWindow; width: 960; height: 640; visible: true }
    QtObject {
        id: ime
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
    }
    Component {
        id: searchComponent
        Reader.SearchSheet { width: 960; height: 640; open: true; inputMethod: ime }
    }
    Component {
        id: selectionComponent
        Reader.SelectionMenu {
            width: 960; height: 640; shown: true; inputMethod: ime
            sel: ({ x: 200, y: 450, w: 100, h: 16 })
        }
    }
    Component {
        id: bookmarksComponent
        Reader.LeftPanel { width: 960; height: 640; open: true; activeTab: "bookmarks" }
    }
    Component {
        id: overlayWindowComponent
        Window {
            width: 960; height: 640; visible: true
            property alias keyboard: keyboardSender
            // A key sender in this window, with no independently scheduled test cases.
            TestCase { id: keyboardSender; name: ""; when: false; optional: true }
        }
    }
    SignalSpy { id: saved; signalName: "noteSaved" }
    SignalSpy { id: dismissed; signalName: "dismissed" }
    SignalSpy { id: deleted; signalName: "bookmarkDeleted" }
    SignalSpy { id: activated; signalName: "bookmarkActivated" }

    function init() {
        ime.visible = false
        ime.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        saved.clear(); dismissed.clear(); deleted.clear(); activated.clear()
        testWindow.requestActivate()
        mouseMove(testWindow.contentItem, 950, 630)
    }
    function makeComponent(factory, properties) {
        const item = createTemporaryObject(factory, testWindow.contentItem, properties || {})
        verify(item !== null)
        wait(40)
        return item
    }
    function child(item, name) {
        const found = findChild(item, name)
        verify(found !== null, name)
        return found
    }
    function tap(item) {
        const points = touchEvent(item)
        const x = item.width / 2, y = item.height / 2
        points.press(0, item, x, y).commit()
        wait(25)
        points.release(0, item, x, y).commit()
    }
    function results() {
        const rows = []
        for (var i = 0; i < 24; ++i)
            rows.push({ cfi: "hit-" + i, chapterTitle: "Chapter " + i,
                        excerpt: { pre: "Before ", match: "word", post: " after." } })
        return rows
    }
    function bookmarks() {
        const rows = []
        for (var i = 0; i < 32; ++i)
            rows.push({ id: "bookmark-" + i, cfi: "cfi-" + i,
                        label: "Chapter " + i, text: "Bookmarked passage " + i })
        return rows
    }

    function test_search_preserves_desktop_geometry_and_caps_touch_results() {
        const sheet = makeComponent(searchComponent, { touchInput: false, results: results() })
        const card = child(sheet, "reader2SearchCard")
        tryCompare(card, "height", 347)
        compare(card.width, 520); compare(card.y, 68)
        ime.keyboardRectangle = Qt.rect(0, 260, 960, 380)
        ime.visible = true
        compare(sheet.keyboardOverlap, 0)
        compare(card.height, 347); compare(card.y, 68)
        sheet.touchInput = true
        tryCompare(sheet, "keyboardOverlap", 380)
        compare(card.y, 68)
        compare(card.height, 182)
        verify(card.y + card.height <= 250)
        const input = child(sheet, "reader2SearchInput")
        const p = input.mapToItem(sheet.surface, 0, 0)
        verify(p.y >= 0 && p.y + input.height <= 260)
    }
    function test_search_window_coordinates_and_adjust_resize() {
        const sheet = makeComponent(searchComponent, { touchInput: true, y: 70, height: 500, results: results() })
        const card = child(sheet, "reader2SearchCard")
        ime.keyboardRectangle = Qt.rect(0, 350, 960, 290)
        ime.visible = true
        tryCompare(sheet, "keyboardOverlap", 220)
        verify(card.y + card.height <= 270)
        sheet.height = 280
        tryCompare(sheet, "keyboardOverlap", 0)
        verify(card.y + card.height <= 270)
        ime.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        compare(sheet.keyboardOverlap, 0)
    }
    function test_note_editor_preserves_size_and_save_above_keyboard() {
        const menu = makeComponent(selectionComponent, { touchInput: false })
        const card = child(menu, "reader2SelectionCard")
        compare(card.height, 44)
        const rowWidth = card.width
        menu.touchInput = true
        compare(card.height, 44); compare(card.width, rowWidth)
        menu.noteEditing = true
        tryCompare(card, "height", 128)
        compare(card.width, 300)
        const note = child(menu, "reader2SelectionNoteInput")
        note.text = "Keep this draft"
        ime.keyboardRectangle = Qt.rect(0, 300, 960, 340)
        ime.visible = true
        tryCompare(menu, "keyboardOverlap", 340)
        compare(card.height, 128); compare(card.width, 300)
        verify(card.y >= 0 && card.y + card.height <= 300)
        const save = child(menu, "reader2SelectionSave")
        const p = save.mapToItem(menu.surface, 0, 0)
        verify(p.y >= 0 && p.y + save.height <= 300)
        const field = note.parent
        const f = field.mapToItem(menu.surface, 0, 0)
        verify(f.y >= 0 && f.y + field.height <= 300)
        saved.target = menu
        tap(save)
        tryCompare(saved, "count", 1)
        compare(saved.signalArguments[0][0], "Keep this draft")
        menu.touchInput = false
        compare(menu.keyboardOverlap, 0)
        compare(card.height, 128); compare(card.width, 300)
    }
    function test_bookmark_delete_is_visible_to_touch_without_row_activation() {
        const panel = makeComponent(bookmarksComponent, { touchInput: true, bookmarks: bookmarks() })
        wait(350)
        const remove = child(panel, "reader2BookmarkDelete_0")
        const area = child(panel, "reader2BookmarkDeleteTouch_0")
        verify(remove.visible)
        const originalWidth = remove.width, originalHeight = remove.height
        deleted.target = panel; activated.target = panel
        tap(area)
        tryCompare(deleted, "count", 1)
        compare(deleted.signalArguments[0][0], "bookmark-0")
        compare(activated.count, 0)
        panel.touchInput = false
        mouseMove(testWindow.contentItem, 950, 630)
        tryCompare(remove, "visible", false)
        compare(remove.width, originalWidth); compare(remove.height, originalHeight)
    }
    function test_reparented_selection_surface_keeps_focus_and_escape_routing() {
        const menu = makeComponent(selectionComponent, { touchInput: true })
        const note = child(menu, "reader2SelectionNoteInput")
        const color = child(menu, "reader2SelectionFirstColor")
        const overlay = createTemporaryObject(overlayWindowComponent, testCase)
        verify(overlay !== null)
        const originalParent = menu.surface.parent
        dismissed.target = menu
        try {
            // ReaderOverlay uses this same visual-parent topology on Android. The
            // host check deliberately does not instantiate a WebView/WindowContainer.
            menu.surface.parent = overlay.contentItem
            overlay.requestActivate()
            menu.noteEditing = true
            note.forceActiveFocus(Qt.OtherFocusReason)
            tryCompare(note, "activeFocus", true)
            verify(menu.focusBelongsHere(note), "native child-window controls belong to this menu")
            const event = { key: Qt.Key_Tab, modifiers: Qt.NoModifier, accepted: false }
            compare(menu.trapTab(event), true)
            verify(event.accepted)
            verify(overlay.activeFocusItem !== note)
            verify(menu.focusBelongsHere(overlay.activeFocusItem))
            menu.noteEditing = false
            color.forceActiveFocus(Qt.OtherFocusReason)
            tryCompare(color, "activeFocus", true)
            overlay.keyboard.keyClick(Qt.Key_Escape)
            tryCompare(dismissed, "count", 1)
        } finally {
            menu.surface.parent = originalParent
        }
    }
    function test_swipe_from_bookmark_delete_yields_to_list() {
        const panel = makeComponent(bookmarksComponent, { touchInput: true, bookmarks: bookmarks() })
        wait(350)
        const list = child(panel, "reader2Bookmarks")
        const area = child(panel, "reader2BookmarkDeleteTouch_2")
        deleted.target = panel; activated.target = panel
        const start = area.mapToItem(list, area.width / 2, area.height / 2)
        const initialY = list.contentY
        const points = touchEvent(list)
        points.press(0, list, start.x, start.y).commit()
        try {
            wait(25)
            for (var offset = 20; offset <= 100; offset += 20) {
                points.move(0, list, start.x, start.y - offset).commit()
                wait(20)
            }
        } finally {
            points.release(0, list, start.x, start.y - 100).commit()
        }
        verify(list.contentY > initialY, "bookmark deletion must yield to a finger drag")
        compare(deleted.count, 0); compare(activated.count, 0)
    }
}

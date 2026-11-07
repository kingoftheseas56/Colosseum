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
    function test_note_editor_preserves_size_and_save_above_keyboard_data() {
        return [{ tag: "wide", w: 960 }, { tag: "narrow-360", w: 360 }]
    }
    function test_note_editor_preserves_size_and_save_above_keyboard(data) {
        const menu = makeComponent(selectionComponent, { touchInput: false, width: data.w })
        const card = child(menu, "reader2SelectionCard")
        compare(card.height, 44)
        const rowWidth = card.width
        menu.touchInput = true
        // BR-READER-TARGETS: touch hosts are 48 units; desktop geometry is restored below.
        tryCompare(card, "height", data.w === 360 ? 96 : 48)
        if (data.w === 360) compare(card.width, 336)
        else tryVerify(function () { return card.width > rowWidth }, 1000, "touch row " + card.width + " vs " + rowWidth)
        const row = child(menu, "reader2SelectionFirstColor").parent.parent
        for (let i = 0; i < row.children.length; ++i) {
            if (row.children[i].label === "Note") tap(row.children[i])
        }
        tryCompare(menu, "noteEditing", true)
        tryCompare(card, "height", 152)
        compare(card.width, 300)
        const note = child(menu, "reader2SelectionNoteInput")
        note.text = "Keep this draft"
        ime.keyboardRectangle = Qt.rect(0, 300, 960, 340)
        ime.visible = true
        tryCompare(menu, "keyboardOverlap", 340)
        compare(card.height, 152); compare(card.width, 300)
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
        verify(save.width >= 48 && save.height >= 48)
        menu.touchInput = false
        compare(menu.keyboardOverlap, 0)
        compare(card.height, 128); compare(card.width, 300)
        menu.noteEditing = false
        compare(card.height, 44); tryCompare(card, "width", rowWidth)
    }
    function rectIn(item, ancestor) {
        const p = item.mapToItem(ancestor, 0, 0)
        return { x: p.x, y: p.y, w: item.width, h: item.height }
    }
    function overlaps(a, b) {
        return a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5 && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5
    }
    function test_selection_touch_targets_48_disjoint_and_contained_data() {
        return [{ tag: "1185x533", w: 1185, h: 533, mode: "select" },
                { tag: "narrow-360", w: 360, h: 640, mode: "select" },
                { tag: "narrow-320", w: 320, h: 640, mode: "select" },
                { tag: "existing-360", w: 360, h: 640, mode: "existing" },
                { tag: "existing-240", w: 240, h: 640, mode: "existing" },
                { tag: "short-landscape", w: 800, h: 300, mode: "select" }]
    }
    function test_selection_touch_targets_48_disjoint_and_contained(data) {
        const menu = makeComponent(selectionComponent, { touchInput: true, width: data.w, height: data.h, mode: data.mode,
                                                          sel: ({ x: data.w / 2 - 40, y: data.h / 2, w: 80, h: 16 }) })
        const card = child(menu, "reader2SelectionCard")
        verify(card.x >= 0 && card.x + card.width <= data.w + 0.5, "card inside width " + card.x + "+" + card.width)
        verify(card.y >= 0 && card.y + card.height <= data.h + 0.5)
        const first = child(menu, "reader2SelectionFirstColor")
        const row = first.parent.parent
        const rects = []
        for (let i = 0; i < row.children.length; ++i) {
            const c = row.children[i]
            if (!c.visible || c.width <= 1) continue
            verify(c.width >= 48 && c.height >= 48, "action host " + i + " " + c.width + "x" + c.height)
            const r = rectIn(c, card)
            verify(r.x >= 0 && r.x + r.w <= card.width + 0.5, "action " + i + " inside card width")
            verify(r.y >= 0 && r.y + r.h <= card.height + 0.5, "action " + i + " inside card height")
            for (let j = 0; j < rects.length; ++j) verify(!overlaps(rects[j], r), "disjoint " + j + "/" + i)
            rects.push(r)
        }
        compare(rects.length, data.mode === "select" ? 6 : 4)
        compare(card.height, data.w <= (data.mode === "select" ? 360 : 240) ? 96 : 48)
        // Edge tap on the first swatch picks only that color.
        const picked = []
        menu.colorPicked.connect(function (c) { picked.push(c) })
        const points = touchEvent(first)
        points.press(0, first, 2, 2).commit(); wait(20); points.release(0, first, 2, 2).commit()
        tryVerify(function () { return picked.length === 1 })
        compare(picked[0], "#F0C24A")
        // The last action remains reachable even when it moves onto another row.
        const actions = []
        menu.copyRequested.connect(function () { actions.push("Copy") })
        menu.deleteRequested.connect(function () { actions.push("Delete") })
        const label = data.mode === "select" ? "Copy" : "Delete"
        for (let k = 0; k < row.children.length; ++k) {
            if (row.children[k].visible && row.children[k].label === label) tap(row.children[k])
        }
        tryCompare(actions, "length", 1)
        compare(actions[0], label)
    }
    function test_bookmark_touch_delete_gutter_is_reserved() {
        const panel = makeComponent(bookmarksComponent, { touchInput: true, bookmarks: bookmarks() })
        wait(350)
        const area = child(panel, "reader2BookmarkDeleteTouch_1")
        const row = area.parent
        compare(area.width, 48)
        compare(area.height, row.height)
        compare(area.x + area.width, row.width)
        deleted.target = panel; activated.target = panel
        // Near the gutter's inner edge: delete only, never jump.
        const points = touchEvent(area)
        points.press(0, area, 2, area.height - 2).commit(); wait(20); points.release(0, area, 2, area.height - 2).commit()
        tryCompare(deleted, "count", 1)
        compare(deleted.signalArguments[0][0], "bookmark-1")
        compare(activated.count, 0)
        // Just left of the gutter: jump only.
        const p2 = touchEvent(row)
        p2.press(0, row, row.width - 52, row.height / 2).commit(); wait(20); p2.release(0, row, row.width - 52, row.height / 2).commit()
        tryCompare(activated, "count", 1)
        compare(deleted.count, 1)
        // Desktop restores the glyph-sized control.
        panel.touchInput = false
        const glyph = child(panel, "reader2BookmarkDelete_1")
        compare(area.width, glyph.width + 12)
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

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/comicreader" as ComicReader
import "../../qml/reader2" as Reader2

TestCase {
    id: testCase
    name: "K01ReaderKeyboard"

    Window { id: testWindow; width: 900; height: 640; visible: true }

    Component { id: inputComp; ComicReader.ComicReaderInput {} }
    Component { id: searchComp; Reader2.SearchSheet {} }
    Component { id: selectionComp; Reader2.SelectionMenu {} }
    Component { id: dictComp; Reader2.DictCard {} }
    Component { id: footComp; Reader2.FootnoteCard {} }

    property var input: null
    property var search: null
    property var selection: null
    property var dict: null
    property var foot: null

    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = byName(kids[i], name)
            if (found) return found
        }
        return null
    }

    function isDescendant(item, ancestor) {
        var p = item
        while (p) { if (p === ancestor) return true; p = p.parent }
        return false
    }

    function init() {
        input = inputComp.createObject(testWindow.contentItem, { "width": 900, "height": 640 })
        search = searchComp.createObject(testWindow.contentItem, { "width": 900, "height": 640, "open": false })
        selection = selectionComp.createObject(testWindow.contentItem, { "width": 900, "height": 640, "shown": false })
        dict = dictComp.createObject(testWindow.contentItem, { "width": 900, "height": 640, "shown": false, "dictState": "empty" })
        foot = footComp.createObject(testWindow.contentItem, { "width": 900, "height": 640, "shown": false, "text": "A sufficiently long footnote for keyboard focus." })
        wait(0)
    }

    function cleanup() {
        if (foot) foot.destroy()
        if (dict) dict.destroy()
        if (selection) selection.destroy()
        if (search) search.destroy()
        if (input) input.destroy()
        foot = null; dict = null; selection = null; search = null; input = null
    }

    function test_context_menu_keyboard_sentinel() {
        var calls = []
        var record = function(x, y) { calls.push([x, y]) }
        input.openContextMenu.connect(record)
        compare(input.keyAction(Qt.Key_Menu, Qt.NoModifier), "openContextMenu")
        compare(input.keyAction(Qt.Key_F10, Qt.ShiftModifier), "openContextMenu")
        compare(calls.length, 2)
        compare(calls[0][0], -1); compare(calls[0][1], -1)
        compare(calls[1][0], -1); compare(calls[1][1], -1)
        input.openContextMenu.disconnect(record)
    }

    function test_search_tab_wraps_both_directions() {
        search.results = [{ cfi: "epubcfi(/6/2)", chapterTitle: "One", excerpt: { pre: "", match: "x", post: "" } }]
        search.resultCount = 1
        search.open = true
        wait(240)
        var field = byName(search, "reader2SearchInput")
        var results = byName(search, "reader2SearchResults")
        verify(field !== null && results !== null)
        compare(testWindow.activeFocusItem, field)
        verify(results.enabled && results.visible && results.activeFocusOnTab, "results region focus eligibility")
        results.forceActiveFocus(Qt.OtherFocusReason)
        compare(testWindow.activeFocusItem, results, "results direct focus")
        field.forceActiveFocus(Qt.OtherFocusReason)
        compare(testWindow.activeFocusItem, field)
        keyClick(Qt.Key_Tab)
        compare(testWindow.activeFocusItem, results)
        keyClick(Qt.Key_Tab)
        compare(testWindow.activeFocusItem, field)
        keyClick(Qt.Key_Tab, Qt.ShiftModifier)
        compare(testWindow.activeFocusItem, results)
        search.open = false
    }

    function test_selection_menu_focus_is_contained() {
        selection.shown = true
        wait(0)
        var first = byName(selection, "reader2SelectionFirstColor")
        verify(first !== null)
        compare(testWindow.activeFocusItem, first)
        keyClick(Qt.Key_Tab, Qt.ShiftModifier)
        verify(isDescendant(testWindow.activeFocusItem, selection), "Shift+Tab must remain inside selection popover")
        keyClick(Qt.Key_Tab)
        verify(isDescendant(testWindow.activeFocusItem, selection), "Tab must remain inside selection popover")
        selection.shown = false
    }

    function test_dictionary_tab_loop_and_footnote_single_region() {
        dict.shown = true
        wait(0)
        var external = byName(dict, "reader2DictExternal")
        verify(external !== null)
        compare(testWindow.activeFocusItem, dict)
        keyClick(Qt.Key_Tab)
        compare(testWindow.activeFocusItem, external)
        keyClick(Qt.Key_Tab)
        compare(testWindow.activeFocusItem, dict)
        dict.shown = false

        foot.shown = true
        wait(0)
        compare(testWindow.activeFocusItem, foot)
        keyClick(Qt.Key_Tab)
        compare(testWindow.activeFocusItem, foot)
        keyClick(Qt.Key_Tab, Qt.ShiftModifier)
        compare(testWindow.activeFocusItem, foot)
        foot.shown = false
    }
}

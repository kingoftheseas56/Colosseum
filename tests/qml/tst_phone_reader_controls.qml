import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml/reader2" as Reader2

TestCase {
    id: testCase
    name: "PhoneReaderControls"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        visible: true
    }

    QtObject {
        id: simulatedInputMethod
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
    }

    Component {
        id: searchSheetComponent
        Reader2.SearchSheet { anchors.fill: parent }
    }

    Component {
        id: leftPanelComponent
        Reader2.LeftPanel { anchors.fill: parent }
    }

    Component {
        id: selectionMenuComponent
        Reader2.SelectionMenu { anchors.fill: parent }
    }

    SignalSpy { id: bookmarkDeleteSpy; signalName: "bookmarkDeleted" }
    SignalSpy { id: bookmarkActivateSpy; signalName: "bookmarkActivated" }
    SignalSpy { id: searchResultSpy; signalName: "resultActivated" }
    SignalSpy { id: defineSpy; signalName: "defineRequested" }
    SignalSpy { id: copySpy; signalName: "copyRequested" }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function findClippedFlickable(item) {
        let current = item ? item.parent : null
        while (current) {
            if (current.clip === true
                    && current.contentWidth !== undefined
                    && current.contentHeight !== undefined)
                return current
            current = current.parent
        }
        return null
    }

    function touchItem(item) {
        const root = testWindow.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(20)
    }

    function searchResults() {
        const rows = []
        for (let i = 0; i < 12; ++i) {
            rows.push({
                "cfi": "epubcfi(/6/" + (i + 2) + "!/4/2/1:0)",
                "chapterTitle": "Chapter " + (i + 1),
                "excerpt": { "pre": "The ", "match": "old", "post": " book opened." }
            })
        }
        return rows
    }

    function bookmarks() {
        return [
            { "id": "phone-bookmark-1", "cfi": "epubcfi(/6/2!/4/2/1:0)",
              "label": "Chapter One", "text": "A saved passage for the first bookmark." },
            { "id": "phone-bookmark-2", "cfi": "epubcfi(/6/4!/4/2/1:0)",
              "label": "Chapter Two", "text": "A second saved passage for the local fixture." }
        ]
    }

    function exerciseSearchSize(sheet, card, resultList, width, height, expectedCfi) {
        testWindow.width = width
        testWindow.height = height
        wait(60)

        compare(sheet.open, true)
        compare(sheet.width, width)
        compare(sheet.height, height)
        const remainingHeight = sheet.height - card.y - card.height
        verify(card.visible && card.width > 0 && card.height >= 50)
        verify(card.x >= -0.5 && card.y >= -0.5,
               "search card should not start outside the host")
        verify(card.x + card.width <= sheet.width + 0.5,
               "search card should fit the host width")
        verify(card.y + card.height <= sheet.height + 0.5,
               "search card should fit the host height")
        verify(remainingHeight >= 9.5,
               "search card should preserve its bottom inset; remaining=" + remainingHeight)
        verify(resultList.visible && resultList.height >= 48,
               "populated search results need a useful, nonnegative viewport")
        verify(resultList.contentHeight > resultList.height,
               "fixture results should remain scrollable")

        const listPoint = resultList.mapToItem(card, 0, 0)
        verify(listPoint.y >= 51,
               "result viewport should begin below the input row and divider")
        verify(listPoint.y + resultList.height <= card.height + 0.5,
               "result viewport should fit inside the search card")

        resultList.positionViewAtIndex(0, ListView.Beginning)
        resultList.forceLayout()
        tryVerify(function() { return resultList.itemAtIndex(0) !== null })
        const firstResult = resultList.itemAtIndex(0)
        verify(firstResult !== null, "the first local search result should be instantiated")
        if (!firstResult)
            return
        const resultPoint = firstResult.mapToItem(resultList, 0, 0)
        verify(resultPoint.y >= -0.5 && resultPoint.y + firstResult.height <= resultList.height + 0.5,
               "the first result should be fully reachable in the visible viewport")

        searchResultSpy.clear()
        touchItem(firstResult)
        tryCompare(searchResultSpy, "count", 1)
        compare(searchResultSpy.signalArguments[0][0], expectedCfi)
    }

    function test_search_card_and_result_touch_fit_portrait_landscape_and_short_resize() {
        testWindow.width = 354
        testWindow.height = 792
        const sheet = createTemporaryObject(searchSheetComponent, testWindow.contentItem)
        verify(sheet !== null)
        if (!sheet)
            return
        sheet.results = searchResults()
        sheet.resultCount = sheet.results.length
        sheet.lastQuery = "fixture"
        sheet.open = true
        searchResultSpy.target = sheet

        const card = findChild(testWindow.contentItem, "reader2SearchCard")
        const resultList = findChild(testWindow.contentItem, "reader2SearchResults")
        verify(card && resultList, "production search sheet should expose its card and results list")
        if (!(card && resultList))
            return

        exerciseSearchSize(sheet, card, resultList, 354, 792, sheet.results[0].cfi)
        exerciseSearchSize(sheet, card, resultList, 792, 354, sheet.results[0].cfi)
        exerciseSearchSize(sheet, card, resultList, 354, 220, sheet.results[0].cfi)
    }

    function exerciseBookmarkDelete(panel, width, height) {
        testWindow.width = width
        testWindow.height = height
        wait(380)

        compare(panel.open, true)
        compare(panel.touchDelete, true)
        const deleteTarget = findChild(panel, "reader2BookmarkDelete_0")
        verify(deleteTarget !== null, "visible phone bookmark should expose its delete target")
        if (!deleteTarget)
            return
        compare(deleteTarget.visible, true)
        verify(!deleteTarget.containsMouse,
               "touch delete should be visible without hovering over the bookmark row")
        verify(deleteTarget.width >= 48 && deleteTarget.height >= 48,
               "bookmark delete target should be at least 48dp in both dimensions")

        const row = deleteTarget.parent
        const deletePoint = deleteTarget.mapToItem(row, 0, 0)
        verify(deletePoint.x >= -0.5 && deletePoint.x + deleteTarget.width <= row.width + 0.5,
               "bookmark delete gutter should fit its row")

        bookmarkDeleteSpy.clear()
        bookmarkActivateSpy.clear()
        touchItem(deleteTarget)
        tryCompare(bookmarkDeleteSpy, "count", 1)
        compare(bookmarkDeleteSpy.signalArguments[0][0], "phone-bookmark-1")
        compare(bookmarkActivateSpy.count, 0,
                "a delete-gutter touch must not activate the bookmark")
    }

    function test_bookmark_delete_is_visible_and_separate_from_activation_on_phone() {
        testWindow.width = 354
        testWindow.height = 792
        const panel = createTemporaryObject(leftPanelComponent, testWindow.contentItem, {
            "androidHost": true,
            "open": true,
            "activeTab": "bookmarks",
            "bookmarks": bookmarks()
        })
        verify(panel !== null)
        if (!panel)
            return

        bookmarkDeleteSpy.target = panel
        bookmarkActivateSpy.target = panel
        exerciseBookmarkDelete(panel, 354, 792)
        exerciseBookmarkDelete(panel, 792, 354)

        panel.androidHost = false
        testWindow.width = 1280
        testWindow.height = 800
        wait(380)
        compare(panel.touchDelete, false,
                "desktop should retain its hover-reveal delete presentation")
        const desktopDeleteTarget = findChild(panel, "reader2BookmarkDelete_0")
        verify(desktopDeleteTarget !== null)
        compare(desktopDeleteTarget.visible, false)
    }

    function selectionRect(item, card) {
        const point = item.mapToItem(card, 0, 0)
        return { "x": point.x, "y": point.y, "width": item.width, "height": item.height }
    }

    function rectsOverlap(a, b) {
        return a.x < b.x + b.width - 0.5 && b.x < a.x + a.width - 0.5
            && a.y < b.y + b.height - 0.5 && b.y < a.y + a.height - 0.5
    }

    function assertSelectionActions(menu, card, note, define, copy, width, height, selection) {
        testWindow.width = width
        testWindow.height = height
        menu.sel = selection
        wait(60)

        compare(menu.phoneLayout, true)
        verify(card.visible && card.width > 0 && card.height > 0)
        const cardPoint = card.mapToItem(menu, 0, 0)
        verify(cardPoint.x >= -0.5 && cardPoint.y >= -0.5)
        verify(cardPoint.x + card.width <= menu.width + 0.5,
               "selection card should fit the phone width")
        verify(cardPoint.y + card.height <= menu.height + 0.5,
               "selection card should fit the phone height")

        const actions = [note, define, copy]
        const rects = []
        for (let i = 0; i < actions.length; ++i) {
            const action = actions[i]
            verify(action.visible, "selection text action should remain visible")
            verify(action.width >= 48 && action.height >= 48,
                   "Note, Define, and Copy need 48dp touch targets")
            const rect = selectionRect(action, card)
            verify(rect.x >= -0.5 && rect.y >= -0.5
                   && rect.x + rect.width <= card.width + 0.5
                   && rect.y + rect.height <= card.height + 0.5,
                   "selection action should fit inside its card")
            const pointInWindow = action.mapToItem(testWindow.contentItem, 0, 0)
            verify(pointInWindow.x >= -0.5 && pointInWindow.y >= -0.5
                   && pointInWindow.x + action.width <= width + 0.5
                   && pointInWindow.y + action.height <= height + 0.5,
                   "selection action should fit inside the phone window")
            rects.push(rect)
        }
        verify(!rectsOverlap(rects[0], rects[1]), "Note and Define targets must not overlap")
        verify(!rectsOverlap(rects[0], rects[2]), "Note and Copy targets must not overlap")
        verify(!rectsOverlap(rects[1], rects[2]), "Define and Copy targets must not overlap")
    }

    function exerciseNoteAboveKeyboard(menu, card, note, noteInput,
                                       width, height, keyboardHeight, selection) {
        testWindow.width = width
        testWindow.height = height
        menu.sel = selection
        simulatedInputMethod.visible = true
        simulatedInputMethod.keyboardRectangle = Qt.rect(0, height - keyboardHeight,
                                                         width, keyboardHeight)
        menu.inputMethod = simulatedInputMethod
        wait(80)

        verify(note.visible, "Note action should be available before opening the editor")
        touchItem(note)
        tryCompare(menu, "noteEditing", true)
        wait(80)
        tryVerify(function() { return menu.keyboardOverlap > 0 && card.height > 0 })

        const keyboardTop = menu.mapFromItem(null, 0,
                                             simulatedInputMethod.keyboardRectangle.y).y
        const cardRect = selectionRect(card, menu)
        const inputRect = selectionRect(noteInput, card)
        const inputPoint = noteInput.mapToItem(testWindow.contentItem, 0, 0)
        const noteViewport = findClippedFlickable(noteInput)
        verify(noteViewport !== null && noteViewport.visible
               && noteViewport.width > 0 && noteViewport.height > 0,
               "note input should have a visible, clipped Flickable viewport")
        let viewportIntersection = { "width": 0, "height": 0 }
        if (noteViewport) {
            const inputInViewport = noteInput.mapToItem(noteViewport, 0, 0)
            const intersectionLeft = Math.max(0, inputInViewport.x)
            const intersectionTop = Math.max(0, inputInViewport.y)
            const intersectionRight = Math.min(noteViewport.width,
                                               inputInViewport.x + noteInput.width)
            const intersectionBottom = Math.min(noteViewport.height,
                                                inputInViewport.y + noteInput.height)
            viewportIntersection = {
                "width": Math.max(0, intersectionRight - intersectionLeft),
                "height": Math.max(0, intersectionBottom - intersectionTop)
            }
        }
        console.log("PHONE_READER_NOTE_IME " + width + "x" + height
                    + " keyboardTop=" + keyboardTop
                    + " overlap=" + menu.keyboardOverlap
                    + " available=" + menu.availableHeight
                    + " noteVisible=" + noteInput.visible
                    + " noteFocus=" + noteInput.focus
                    + " noteActiveFocus=" + noteInput.activeFocus
                    + " windowActive=" + testWindow.active
                    + " focusItem=" + (testWindow.activeFocusItem
                                       ? testWindow.activeFocusItem.objectName : "")
                    + " card=" + JSON.stringify(cardRect)
                    + " input=" + JSON.stringify(inputRect)
                    + " viewport=" + JSON.stringify(noteViewport
                                                      ? { "width": noteViewport.width,
                                                          "height": noteViewport.height }
                                                      : null)
                    + " visibleIntersection=" + JSON.stringify(viewportIntersection))

        verify(menu.availableHeight <= keyboardTop + 0.5,
               "available reader height should stop at the simulated keyboard")
        verify(card.visible && card.height > 0
               && cardRect.x >= -0.5 && cardRect.y >= -0.5
               && cardRect.x + card.width <= menu.width + 0.5
               && cardRect.y + card.height <= keyboardTop + 0.5,
               "note card should remain bounded above the simulated keyboard")
        verify(noteInput.visible && noteInput.focus,
               "a genuine Note touch should focus the production note editor")
        verify(inputRect.x >= -0.5 && inputRect.y >= -0.5
               && inputRect.x + noteInput.width <= card.width + 0.5
               && inputRect.y + noteInput.height <= card.height + 0.5,
               "note input should fit inside the editor card")
        verify(inputPoint.x >= -0.5 && inputPoint.y >= -0.5
               && inputPoint.x + noteInput.width <= width + 0.5
               && inputPoint.y + noteInput.height <= keyboardTop + 0.5,
               "note input should remain inside the window and above the simulated keyboard")
        verify(viewportIntersection.width > 0 && viewportIntersection.height > 0,
               "note input should have a positive visible intersection with its clipped viewport")
        if (width === 792)
            verify(card.height < 180,
                   "landscape IME overlap should shrink the expanded note card")
    }

    function test_selection_actions_fit_both_orientations_and_note_opens_editor() {
        testWindow.width = 354
        testWindow.height = 792
        const menu = createTemporaryObject(selectionMenuComponent, testWindow.contentItem, {
            "androidHost": true,
            "shown": true,
            "mode": "select",
            "sel": { "x": 122, "y": 350, "w": 64, "h": 18 }
        })
        verify(menu !== null)
        if (!menu)
            return

        defineSpy.target = menu
        copySpy.target = menu
        const card = findChild(testWindow.contentItem, "reader2SelectionCard")
        const note = findChild(testWindow.contentItem, "reader2SelectionNote")
        const define = findChild(testWindow.contentItem, "reader2SelectionDefine")
        const copy = findChild(testWindow.contentItem, "reader2SelectionCopy")
        const noteInput = findChild(testWindow.contentItem, "reader2SelectionNoteInput")
        verify(card && note && define && copy && noteInput,
               "production selection menu should expose Note, Define, Copy, and its note input")
        if (!(card && note && define && copy && noteInput))
            return

        assertSelectionActions(menu, card, note, define, copy, 354, 792,
                               { "x": 122, "y": 350, "w": 64, "h": 18 })
        assertSelectionActions(menu, card, note, define, copy, 792, 354,
                               { "x": 330, "y": 160, "w": 54, "h": 18 })

        defineSpy.clear()
        touchItem(define)
        tryCompare(defineSpy, "count", 1)
        copySpy.clear()
        touchItem(copy)
        tryCompare(copySpy, "count", 1)
        exerciseNoteAboveKeyboard(menu, card, note, noteInput, 354, 792, 300,
                                  { "x": 122, "y": 350, "w": 64, "h": 18 })
        exerciseNoteAboveKeyboard(menu, card, note, noteInput, 792, 354, 220,
                                  { "x": 330, "y": 160, "w": 54, "h": 18 })
    }
}

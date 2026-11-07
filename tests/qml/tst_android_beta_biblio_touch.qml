import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidBetaBiblioTouch"
    when: windowShown

    Window { id: testWindow; width: 960; height: 640; visible: true }
    Component {
        id: pageComponent
        App.BiblioLibraryPage { width: 960; height: 640; active: false; touchInput: true }
    }
    Component {
        id: flowComponent
        Flickable {
            id: flowHost
            width: 960; height: 640; contentWidth: width; contentHeight: page.flowHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            property alias page: page
            App.BiblioLibraryPage {
                id: page
                width: parent.width; height: flowHeight; active: false; touchInput: true
                pageFlick: flowHost
            }
        }
    }
    SignalSpy { id: details; signalName: "detailRequested" }
    SignalSpy { id: resumes; signalName: "resumeRequested" }

    function rows() {
        var result = []
        for (var i = 0; i < 48; ++i)
            result.push({ entry: { id: "touch-" + i, title: "Book " + i },
                          canResume: i === 1, progressRecord: { id: "resume-" + i },
                          cover: "", author: "Author", progress: 0 })
        return result
    }
    function populate(page) {
        const model = rows()
        page.allRows = model
        page.visibleRows = model
        details.target = page; resumes.target = page
        details.clear(); resumes.clear()
        wait(50)
        return page
    }
    function page() {
        testWindow.requestActivate()
        const item = createTemporaryObject(pageComponent, testWindow.contentItem)
        verify(item !== null)
        return populate(item)
    }
    function area(page, index) {
        const item = findChild(page, "biblioLibraryCardTouch_touch-" + index)
        verify(item !== null)
        return item
    }
    function tap(item) {
        const points = touchEvent(item)
        points.press(0, item, item.width / 2, item.height / 2).commit()
        wait(25)
        points.release(0, item, item.width / 2, item.height / 2).commit()
    }
    function swipe(item) {
        const points = touchEvent(item)
        const x = item.width / 2
        points.press(0, item, x, 165).commit()
        wait(25)
        for (var y = 145; y >= 25; y -= 20) {
            points.move(0, item, x, y).commit()
            wait(20)
        }
        points.release(0, item, x, 25).commit()
    }

    function test_tap_preserves_details_and_resume() {
        const item = page()
        tap(area(item, 0))
        tryCompare(details, "count", 1)
        compare(resumes.count, 0)
        compare(details.signalArguments[0][0].id, "touch-0")
        tap(area(item, 1))
        tryCompare(resumes, "count", 1)
        compare(details.count, 1)
        compare(resumes.signalArguments[0][0].id, "resume-1")
    }
    function test_stationary_hold_opens_menu_without_primary_action() {
        const item = page()
        const card = area(item, 0)
        const points = touchEvent(card)
        points.press(0, card, 70, 100).commit()
        try {
            tryCompare(item, "menuRowId", "touch-0", 1000)
        } finally {
            points.release(0, card, 70, 100).commit()
        }
        wait(40)
        compare(details.count, 0); compare(resumes.count, 0)
        compare(item.menuRowId, "touch-0")
    }
    function test_real_mouse_hold_keeps_primary_click() {
        const item = page()
        const card = area(item, 0)
        mousePress(card, 70, 100)
        try {
            wait(560)
            compare(item.menuRowId, "")
        } finally {
            mouseRelease(card, 70, 100)
        }
        tryCompare(details, "count", 1)
    }
    function test_in_flight_visibility_or_enable_change_cancels_data() {
        return [{ tag: "disabled", property: "enabled" },
                { tag: "hidden", property: "visible" }]
    }
    function test_in_flight_visibility_or_enable_change_cancels(data) {
        const item = page()
        const card = area(item, 0)
        const points = touchEvent(card)
        points.press(0, card, 70, 100).commit()
        try {
            wait(40)
            card[data.property] = false
            wait(40)
            card[data.property] = true
            wait(560)
        } finally {
            card[data.property] = true
            points.release(0, card, 70, 100).commit()
        }
        compare(item.menuRowId, "")
        compare(details.count, 0); compare(resumes.count, 0)
        tap(card)
        tryCompare(details, "count", 1)
    }
    function test_swipe_cancels_card_and_scrolls_standalone_wall() {
        const item = page()
        const wall = item._keyboardWallForTest
        const start = wall.contentY
        const card = area(item, 0)
        swipe(card)
        verify(wall.contentY > start, "finger drag must reach the standalone grid")
        compare(item.menuRowId, "")
        compare(card.holdMenuFired, false)
        compare(details.count, 0); compare(resumes.count, 0)
    }
    function test_page_flow_drag_uses_parent_and_clips_menu() {
        const flow = createTemporaryObject(flowComponent, testWindow.contentItem)
        verify(flow !== null)
        const item = populate(flow.page)
        const wall = item._keyboardWallForTest
        compare(wall.interactive, false)
        const start = flow.contentY
        swipe(area(item, 0))
        verify(flow.contentY > start, "page-flow delegates must yield to the parent")
        compare(item.menuRowId, "")
        compare(details.count, 0); compare(resumes.count, 0)
        flow.cancelFlick()
        flow.contentY = 400
        wait(30)
        const row = item.visibleRows[10]
        const bottom = item.menuViewport.y + item.menuViewport.height
        item.openMenu(row, 300, bottom - 2, wall)
        const menu = findChild(item, "biblioLibraryMenuPanel")
        verify(menu !== null)
        verify(menu.y >= item.menuViewport.y)
        verify(menu.y + menu.height <= bottom,
               "a touch menu must stay inside the visible parent viewport")
        compare(item.holdPointVisible(item, 100, -1), false)
    }
    function test_page_flow_hold_does_not_reposition_focused_wall() {
        testWindow.requestActivate()
        const flow = createTemporaryObject(flowComponent, testWindow.contentItem)
        verify(flow !== null)
        const item = populate(flow.page)
        const wall = item._keyboardWallForTest
        flow.contentY = 200
        wall.forceActiveFocus(Qt.OtherFocusReason)
        wait(40)
        const card = area(item, 5)
        const initialY = flow.contentY
        const points = touchEvent(card)
        points.press(0, card, 70, 80).commit()
        try {
            tryCompare(item, "menuRowId", "touch-5", 1000)
            wait(300)
        } finally {
            points.release(0, card, 70, 80).commit()
        }
        verify(Math.abs(flow.contentY - initialY) < 0.5,
               "opening a touch menu must not invoke keyboard scrolling")
        compare(item.menuRowId, "touch-5")
        compare(details.count, 0); compare(resumes.count, 0)
    }
}

import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidLibraryTouch"
    when: windowShown

    Window {
        id: testWindow
        width: 1000; height: 650; visible: true
        Flickable {
            id: pageFlick
            width: parent.width; height: parent.height
            contentWidth: width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
        }
    }
    Component { id: theatreComponent; App.LibraryPage { width: 960; active: false; touchInput: true } }
    Component { id: tankobanComponent; App.TankobanLibraryTab { width: 960; active: false; touchInput: true } }
    SignalSpy { id: details; signalName: "detailRequested" }
    SignalSpy { id: resumes; signalName: "resumeRequested" }
    SignalSpy { id: cancellations; signalName: "canceled" }
    property var page: null
    property var pendingTouch: null

    function cases() {
        return [
            { tag: "theatre-wall", world: "theatre", flow: false },
            { tag: "theatre-page", world: "theatre", flow: true },
            { tag: "tankoban-wall", world: "tankoban", flow: false },
            { tag: "tankoban-page", world: "tankoban", flow: true }
        ]
    }
    function rows(world) {
        var result = []
        for (var i = 0; i < 30; ++i)
            result.push({ entry: { id: world + i, title: "Title " + i, cover: "" },
                          state: "unwatched", progress: 0, canResume: false,
                          downloaded: false, newCount: 0, year: 2020, isSeries: false })
        return result
    }
    function createPage(data) {
        testWindow.requestActivate()
        pageFlick.visible = data.flow
        pageFlick.contentY = 0
        pageFlick.contentHeight = pageFlick.height
        const component = data.world === "theatre" ? theatreComponent : tankobanComponent
        page = component.createObject(data.flow ? pageFlick.contentItem : testWindow.contentItem,
                                      { pageFlick: data.flow ? pageFlick : null })
        verify(page !== null)
        page.allRows = rows(data.world)
        page.visibleRows = page.allRows
        if (data.flow) {
            const createdPage = page
            page.height = Qt.binding(function() { return createdPage.flowHeight })
            pageFlick.contentHeight = page.flowHeight
        } else {
            page.height = testWindow.height
        }
        details.target = page; details.clear()
        resumes.target = page; resumes.clear()
        waitForRendering(page)
        tryVerify(function() { return card(data, 0) !== null })
        return page
    }
    function card(data, index) {
        return findChild(page, data.world + "LibraryCardTouch_" + data.world + index)
    }
    function wall(data) { return findChild(page, data.world + "LibraryWall") }
    function menu(data) { return findChild(page, data.world + "LibraryMenuPanel") }
    function centerInWindow(item) {
        return item.mapToItem(testWindow.contentItem, item.width / 2, Math.min(100, item.height / 2))
    }
    function beginTouch(point) {
        pendingTouch = touchEvent(testWindow.contentItem)
        pendingTouch.press(0, testWindow.contentItem, point.x, point.y).commit()
        return pendingTouch
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, testWindow.contentItem, x, y).commit()
        pendingTouch = null
    }
    function tap(item) {
        const point = centerInWindow(item)
        beginTouch(point)
        wait(25)
        releaseTouch(point.x, point.y)
    }
    function cleanup() {
        // Assertion failures must not leave a pressed point in the next data row.
        releaseTouch(-50, -50)
        cancellations.target = null
        details.target = null; resumes.target = null
        if (page) { page.closeMenu(false); page.destroy() }
        page = null
        pageFlick.cancelFlick()
        pageFlick.contentY = 0
        pageFlick.contentHeight = pageFlick.height
    }

    function test_touch_tap_hold_and_release_data() { return cases() }
    function test_touch_tap_hold_and_release(data) {
        createPage(data)
        const area = card(data, 0)
        tap(area)
        tryCompare(details, "count", 1)
        compare(details.signalArguments[0][0].id, data.world + "0")
        const point = centerInWindow(area)
        beginTouch(point)
        tryCompare(page, "menuRowId", data.world + "0", 1200)
        releaseTouch(point.x, point.y)
        wait(60)
        compare(details.count, 1, "Release after a hold must not open details")
        compare(resumes.count, 0, "Release after a hold must not resume")
        page.closeMenu(false)
        wait(30)
        tap(area)
        tryCompare(details, "count", 2, 1000)
    }

    function test_drag_cancels_hold_and_scrolls_owner_data() { return cases() }
    function test_drag_cancels_hold_and_scrolls_owner(data) {
        createPage(data)
        const area = card(data, 0)
        const owner = data.flow ? pageFlick : wall(data)
        cancellations.target = area; cancellations.clear()
        compare(wall(data).interactive, !data.flow)
        const before = owner.contentY
        const point = centerInWindow(area)
        const sequence = beginTouch(point)
        tryCompare(area, "pressed", true)
        wait(20)
        sequence.move(0, testWindow.contentItem, point.x, point.y - 40).commit()
        wait(20)
        sequence.move(0, testWindow.contentItem, point.x, point.y - 90).commit()
        wait(20)
        sequence.move(0, testWindow.contentItem, point.x, point.y - 150).commit()
        wait(20)
        releaseTouch(point.x, point.y - 150)
        tryVerify(function() { return owner.contentY > before + 30 })
        verify(cancellations.count > 0, "The scrolling owner must cancel the card press")
        compare(area.holdMenuFired, false)
        wait(550)
        compare(page.menuRow, null)
        compare(details.count, 0)
        compare(resumes.count, 0)
    }

    function test_disable_does_not_activate_data() {
        const data = cases()
        return data.concat(cases().map(function(row) {
            return { tag: row.tag + "-reenabled", world: row.world, flow: row.flow, reenable: true }
        }))
    }
    function test_disable_does_not_activate(data) {
        createPage(data)
        const area = card(data, 0)
        const point = centerInWindow(area)
        beginTouch(point)
        tryCompare(area, "pressed", true)
        area.enabled = false
        if (data.reenable) { wait(25); area.enabled = true }
        wait(550)
        compare(page.menuRow, null)
        releaseTouch(point.x, point.y)
        compare(area.holdMenuFired, false)
        compare(details.count, 0)
        area.enabled = true
        tap(area)
        tryCompare(details, "count", 1)
    }

    function test_mouse_hold_does_not_open_touch_menu_data() { return cases() }
    function test_mouse_hold_does_not_open_touch_menu(data) {
        createPage(data)
        const area = card(data, 0)
        mouseClick(area, area.width / 2, 60, Qt.LeftButton)
        tryCompare(details, "count", 1)
        mousePress(area, area.width / 2, 60, Qt.LeftButton)
        wait(650)
        mouseRelease(area, area.width / 2, 60, Qt.LeftButton)
        compare(page.menuRow, null, "A real mouse hold is not an Android touch hold")
    }

    function test_visible_viewport_and_menu_height_data() { return cases() }
    function test_visible_viewport_and_menu_height(data) {
        createPage(data)
        if (data.flow) {
            pageFlick.contentY = 300
            wait(60)
        }
        const viewport = page.menuViewport
        page.openMenu(page.visibleRows[0], 500, viewport.y + viewport.height - 2, null)
        const panel = menu(data)
        verify(panel !== null)
        verify(panel.height > 0, "The menu must have nonzero paint and hit-test geometry")
        verify(panel.y >= viewport.y + 8)
        verify(panel.y + panel.height <= viewport.y + viewport.height - 8)
        page.closeMenu(false)
        const area = card(data, 0)
        if (area) {
            verify(!page.holdPointVisible(area, -1000, -1000))
            if (data.flow)
                verify(!page.holdPointVisible(area, area.width / 2, 10),
                       "A card above the clipped page cannot open a hold menu")
        }
    }

    function test_tankoban_tap_preserves_resume_route() {
        const data = { world: "tankoban", flow: false }
        createPage(data)
        const updated = rows(data.world)
        updated[0].state = "inProgress"
        updated[0].resumeTarget = { id: "resume-record", kind: "manga" }
        page.visibleRows = updated
        waitForRendering(page)
        tap(card(data, 0))
        tryCompare(resumes, "count", 1)
        compare(resumes.signalArguments[0][0].id, "resume-record")
        compare(details.count, 0)
    }

    function test_hold_keeps_scrolled_card_menu_open_data() {
        return [{ tag: "theatre", world: "theatre", flow: true },
                { tag: "tankoban", world: "tankoban", flow: true }]
    }
    function test_hold_keeps_scrolled_card_menu_open(data) {
        createPage(data)
        const grid = wall(data)
        grid.forceActiveFocus()
        pageFlick.contentY = 300
        wait(60)
        const index = grid.columnCount
        const area = card(data, index)
        verify(area !== null)
        const point = centerInWindow(area)
        verify(point.y > 0 && point.y < testWindow.height)
        beginTouch(point)
        wait(850)
        releaseTouch(point.x, point.y)
        compare(page.menuRowId, data.world + index,
                "Selecting a held visible card must not scroll its new menu away")
        compare(details.count, 0)
        compare(resumes.count, 0)
    }
}

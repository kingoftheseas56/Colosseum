// Real-input contract tests for stationary touch long press on the phone library cards
// (Theatre LibraryPage, TankobanLibraryTab, BiblioLibraryPage) and the paths that must
// survive around it: single short-tap activation, hold-opens-menu without activation,
// release-after-hold swallow, drag/scroll cancel through the owning Flickable (standalone
// wall and nested pageFlow), the still-working 48dp dots button, the menu's own action
// routing, and the desktop keyboard context/activation contract.
import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneLibraryLongPress"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        property bool androidHost: true
        visible: true
    }

    Component { id: theatrePageComponent; App.LibraryPage { anchors.fill: parent } }
    Component { id: biblioPageComponent; App.BiblioLibraryPage { anchors.fill: parent } }
    Component { id: tankobanPageComponent; App.TankobanLibraryTab { anchors.fill: parent } }
    Component { id: theatreFlowComponent; App.LibraryPage {} }
    Component { id: biblioFlowComponent; App.BiblioLibraryPage {} }
    Component { id: tankobanFlowComponent; App.TankobanLibraryTab {} }
    Component {
        id: pageFlickComponent
        Flickable {
            clip: true
            interactive: true
            contentWidth: width
            contentHeight: 12000
        }
    }

    SignalSpy { id: detailSpy; signalName: "detailRequested" }
    SignalSpy { id: resumeSpy; signalName: "resumeRequested" }
    SignalSpy { id: removeSpy; signalName: "removeRequested" }

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

    function findMatching(root, predicate) {
        if (!root)
            return null
        if (predicate(root))
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findMatching(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function pageSpec(name) {
        if (name === "Theatre")
            return {
                standalone: theatrePageComponent, flow: theatreFlowComponent,
                grid: "theatreLibraryGrid", dots: "theatreLibraryCardMenu_",
                panel: "theatreLibraryMenuPanel"
            }
        if (name === "Biblio")
            return {
                standalone: biblioPageComponent, flow: biblioFlowComponent,
                grid: "biblioLibraryGrid", dots: "biblioLibraryCardMenu_",
                panel: "biblioLibraryMenuPanel"
            }
        return {
            standalone: tankobanPageComponent, flow: tankobanFlowComponent,
            grid: "tankobanLibraryGrid", dots: "tankobanLibraryCardMenu_",
            panel: "tankobanLibraryMenuPanel"
        }
    }

    // 24 rows → 8 portrait rows of cards, so the wall/page has real scroll range.
    function fixtures(prefix) {
        const rows = []
        for (let i = 0; i < 24; ++i) {
            rows.push({
                "entry": {
                    "id": prefix + "-" + (i + 1),
                    "title": "Hold Fixture " + (i + 1),
                    "cover": "",
                    "type": "movie"
                },
                "author": "Fixture Author",
                "progress": 0,
                "state": "unwatched",
                "canResume": false,
                "downloaded": false
            })
        }
        return rows
    }

    function wireSpies(page) {
        detailSpy.target = page
        resumeSpy.target = page
        removeSpy.target = page
        detailSpy.clear()
        resumeSpy.clear()
        removeSpy.clear()
    }

    function primaryCount() {
        return detailSpy.count + resumeSpy.count
    }

    function pressAt(item, lx, ly) {
        const root = testWindow.contentItem
        const point = item.mapToItem(root, lx, ly)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        return point
    }

    function releaseAt(x, y) {
        const root = testWindow.contentItem
        touchEvent(root).release(0, root, x, y).commit()
    }

    function touchTapAt(item, lx, ly) {
        const point = pressAt(item, lx, ly)
        wait(40)
        releaseAt(point.x, point.y)
        wait(90)
    }

    // Stationary hold: press, outlast the 500ms hold interval, then release in place.
    function touchHoldAt(item, lx, ly) {
        const point = pressAt(item, lx, ly)
        wait(700)
        releaseAt(point.x, point.y)
        wait(90)
    }

    function touchDragFrom(item, lx, ly, dx, dy) {
        const root = testWindow.contentItem
        const point = pressAt(item, lx, ly)
        wait(40)
        for (let step = 1; step <= 5; ++step) {
            const x = point.x + dx * step / 5
            const y = point.y + dy * step / 5
            touchEvent(root).move(0, root, x, y).commit()
            wait(30)
        }
        releaseAt(point.x + dx, point.y + dy)
        wait(140)
    }

    function findRemoveRowLabel(page) {
        return findMatching(page, function(item) {
            return item.text !== undefined && String(item.text) === "Remove from Library"
        })
    }

    function setupStandalone(name, spec) {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.androidHost = true
        const page = createTemporaryObject(spec.standalone, testWindow.contentItem)
        verify(page !== null, name + " production page should instantiate")
        page.active = true
        page.allRows = fixtures(name)
        page.visibleRows = page.allRows
        wait(120)
        const grid = findChild(page, spec.grid)
        verify(grid !== null, name + " production grid should be discoverable")
        grid.forceLayout()
        tryVerify(function() { return grid.itemAtIndex(0) !== null }, 2000,
                  name + " first card should instantiate")
        return { page: page, grid: grid }
    }

    function runStandaloneSuite(name) {
        const spec = pageSpec(name)
        const env = setupStandalone(name, spec)
        const page = env.page
        const grid = env.grid
        wireSpies(page)
        const card = grid.itemAtIndex(0)
        const fixtureId = name + "-1"
        // Poster centre — inside the card's primary tap surface, clear of the dots corner.
        const px = card.width / 2
        const py = grid.posterH / 2

        // 1. A short tap still activates the card exactly once and opens nothing.
        touchTapAt(card, px, py)
        compare(primaryCount(), 1,
                name + " short poster tap should activate the card exactly once")
        verify(page.menuRow === null, name + " short tap must not open the card menu")

        // 2. A stationary touch hold opens the existing per-item menu for that row only.
        touchHoldAt(card, px, py)
        verify(page.menuRow !== null && page.menuRow.entry
               && String(page.menuRow.entry.id) === fixtureId,
               name + " stationary hold should open the pressed row's card menu")
        compare(primaryCount(), 1,
                name + " hold plus release must not also activate the card")
        const panel = findChild(page, spec.panel)
        verify(panel !== null && panel.visible,
               name + " hold should surface the production menu panel")
        const removeLabel = findRemoveRowLabel(page)
        verify(removeLabel !== null,
               name + " open menu should list its destructive Remove action")
        removeSpy.clear()
        touchTapAt(removeLabel, removeLabel.width / 2, removeLabel.height / 2)
        tryVerify(function() { return removeSpy.count === 1 }, 1500,
                  name + " tapping the menu's Remove row should fire removeRequested once")
        tryVerify(function() { return page.menuRow === null }, 1500,
                  name + " menu should close after its action")
        if (removeSpy.count === 1)
            compare(String(removeSpy.signalArguments[0][0].id), fixtureId,
                    name + " menu action should carry the held row's entry")

        // 3. A drag cancels the hold and scrolls the owning wall instead.
        const contentYBefore = grid.contentY
        touchDragFrom(card, px, py, 0, -90)
        tryVerify(function() { return !grid.moving && !grid.flicking }, 2000,
                  name + " wall should settle after the committed drag")
        wait(600)
        verify(page.menuRow === null,
               name + " drag must cancel the hold before the menu can open")
        compare(primaryCount(), 1, name + " cancelled hold must not activate the card")
        verify(grid.contentY > contentYBefore,
               name + " drag should scroll the owning wall")
        grid.contentY = 0
        wait(80)

        // 4. The phone dots button keeps its own working 48dp path.
        const dots = findChild(page, spec.dots + fixtureId)
        verify(dots !== null && dots.visible && dots.opacity >= 0.99
               && dots.width >= 48 && dots.height >= 48,
               name + " phone dots button should stay visible at 48dp")
        touchTapAt(dots, dots.width / 2, dots.height / 2)
        tryVerify(function() {
            return page.menuRow !== null && page.menuRow.entry
                   && String(page.menuRow.entry.id) === fixtureId
        }, 1500, name + " dots touch should still open the same row's menu")
        compare(primaryCount(), 1, name + " dots menu open must not activate the card")
        page.closeMenu(false)

        // 5. Desktop keyboard contract: context key opens the menu, Enter activates,
        //    focus returns to the wall after the menu closes. The owning window must be
        //    active for key delivery in this harness.
        testWindow.width = 1280
        testWindow.height = 800
        testWindow.androidHost = false
        testWindow.requestActivate()
        tryVerify(function() { return testWindow.active }, 2000,
                  name + " desktop window should activate for keyboard delivery")
        wait(140)
        grid.forceLayout()
        grid.currentIndex = 0
        grid.forceActiveFocus(Qt.OtherFocusReason)
        tryVerify(function() { return grid.activeFocus }, 1500,
                  name + " wall should take focus on desktop")
        keyClick(Qt.Key_Menu)
        let keyboardMenu = page.menuRow !== null
        if (!keyboardMenu) {
            keyClick(Qt.Key_F10, Qt.ShiftModifier)
            keyboardMenu = page.menuRow !== null
        }
        verify(keyboardMenu,
               name + " keyboard context key should still open the card menu")
        verify(page.menuRow && String(page.menuRow.entry.id) === fixtureId,
               name + " keyboard menu should open for the current row")
        page.closeMenu(true)
        tryVerify(function() { return grid.activeFocus }, 1500,
                  name + " menu close should return focus to the wall")
        const primaryBefore = primaryCount()
        keyClick(Qt.Key_Return)
        tryVerify(function() { return primaryCount() === primaryBefore + 1 }, 1500,
                  name + " keyboard Enter should still activate the current row")
    }

    function setupFlow(name, spec) {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.androidHost = true
        const flick = createTemporaryObject(pageFlickComponent, testWindow.contentItem)
        flick.x = 0
        flick.y = 140
        flick.width = 354
        flick.height = 580
        flick.contentY = 0
        const page = createTemporaryObject(spec.flow, flick.contentItem,
                                           { "x": 0, "y": 0, "width": 354, "pageFlick": flick })
        verify(page !== null, name + " page-flow root should instantiate")
        // pageFlick rides creation properties (production mounting): assigning it after
        // creation leaves the wallHost bottom-anchor override latched and the wall
        // collapses to a negative height that receives no input.
        page.active = true
        page.allRows = fixtures(name + "Flow")
        page.visibleRows = page.allRows
        wait(120)
        page.height = page.flowHeight
        flick.contentHeight = page.flowHeight
        wait(100)
        const grid = findChild(page, spec.grid)
        verify(grid !== null, name + " flow grid should be discoverable")
        grid.forceLayout()
        tryVerify(function() { return grid.itemAtIndex(0) !== null }, 2000,
                  name + " flow first card should instantiate")
        return { page: page, grid: grid, flick: flick }
    }

    function runFlowSuite(name) {
        const spec = pageSpec(name)
        const env = setupFlow(name, spec)
        const page = env.page
        const grid = env.grid
        const flick = env.flick
        wireSpies(page)
        const card = grid.itemAtIndex(0)
        const fixtureId = name + "Flow-1"
        const px = card.width / 2
        const py = grid.posterH / 2

        // Hold opens the same menu through the nested pageFlow viewport.
        touchHoldAt(card, px, py)
        verify(page.menuRow !== null && page.menuRow.entry
               && String(page.menuRow.entry.id) === fixtureId,
               name + " flow hold should open the pressed row's card menu")
        compare(primaryCount(), 0,
                name + " flow hold plus release must not activate the card")
        page.closeMenu(false)

        // Drag cancels the hold and scrolls the nested page Flickable.
        touchDragFrom(card, px, py, 0, -90)
        tryVerify(function() { return !flick.moving && !flick.flicking }, 2000,
                  name + " page Flickable should settle after the committed drag")
        wait(600)
        verify(page.menuRow === null,
               name + " drag must cancel the flow hold before the menu opens")
        compare(primaryCount(), 0,
                name + " cancelled flow hold must not activate the card")
        verify(flick.contentY > 0,
               name + " drag should scroll the nested page Flickable")
        flick.contentY = 0
        wait(120)

        // A hold whose card scrolls out from under the stationary finger stays closed.
        const pressPoint = pressAt(card, px, py)
        flick.contentY = Math.min(flick.contentHeight - flick.height, 600)
        wait(700)
        verify(page.menuRow === null,
               name + " hold must not open a menu for a card that left the viewport")
        releaseAt(pressPoint.x, pressPoint.y)
        wait(90)
        compare(primaryCount(), 0,
                name + " scroll-away hold release must not activate the card")
    }

    function test_theatre_long_press_contract() {
        runStandaloneSuite("Theatre")
        runFlowSuite("Theatre")
    }

    function test_biblio_long_press_contract() {
        runStandaloneSuite("Biblio")
        runFlowSuite("Biblio")
    }

    function test_tankoban_long_press_contract() {
        runStandaloneSuite("Tankoban")
        runFlowSuite("Tankoban")
    }
}

import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneLibraryLayouts"
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
    Component { id: theatreFlowPageComponent; App.LibraryPage {} }
    Component { id: biblioFlowPageComponent; App.BiblioLibraryPage {} }
    Component { id: tankobanFlowPageComponent; App.TankobanLibraryTab {} }
    Component {
        id: pageFlickComponent
        Flickable {
            clip: true
            interactive: true
            contentWidth: width
            contentHeight: 12000
        }
    }

    SignalSpy { id: filterPickedSpy; signalName: "picked" }
    SignalSpy { id: filterClickedSpy; signalName: "clicked" }
    SignalSpy { id: detailRequestedSpy; signalName: "detailRequested" }
    SignalSpy { id: resumeRequestedSpy; signalName: "resumeRequested" }

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

    function findSearch(page, objectName) {
        return findChild(page, objectName) || findMatching(page, function(item) {
            return item.placeholderText !== undefined
                && String(item.placeholderText).toLowerCase().indexOf("search") >= 0
        })
    }

    function findGrid(page, objectName) {
        return findChild(page, objectName) || findMatching(page, function(item) {
            return typeof item.cellWidth === "number" && typeof item.itemAtIndex === "function"
        })
    }

    function findText(page, label) {
        return findMatching(page, function(item) {
            return item.text !== undefined && String(item.text) === label
        })
    }

    function findFilterButton(page, objectName, label) {
        const named = objectName ? findChild(page, objectName) : null
        if (named)
            return named
        const text = findText(page, label)
        return text ? text.parent : null
    }

    function checkPhoneCardMenu(page, pageName, expectedProperty) {
        const prefixes = {
            "Theatre": "theatreLibraryCardMenu_",
            "Biblio": "biblioLibraryCardMenu_",
            "Tankoban": "tankobanLibraryCardMenu_"
        }
        const firstFixtureId = pageName + "-1"
        const menuTarget = findChild(page, prefixes[pageName] + firstFixtureId)
        verify(menuTarget !== null, pageName + " first fixture should expose a named card menu target")
        if (!menuTarget)
            return

        verify(menuTarget.visible && menuTarget.opacity >= 0.99,
               pageName + " phone card menu should be visibly rendered")
        verify(menuTarget.width >= 48 && menuTarget.height >= 48,
               pageName + " phone card menu should provide at least a 48dp target; size="
               + menuTarget.width + "x" + menuTarget.height)

        // Restore the ordinary default filter/sort and the filter strip position before
        // exercising the independent card-menu affordance.
        page[expectedProperty] = expectedProperty === "sortMode" ? "lastRead" : ""
        const filterViewport = findChild(page, pageName === "Theatre"
                                         ? "theatreLibraryFilterViewport"
                                         : pageName === "Biblio"
                                           ? "biblioLibraryFilterViewport"
                                           : "tankobanLibraryFilterViewport")
        if (filterViewport)
            filterViewport.contentX = 0
        wait(80)

        detailRequestedSpy.target = page
        resumeRequestedSpy.target = page
        detailRequestedSpy.clear()
        resumeRequestedSpy.clear()
        const menuPoint = menuTarget.mapToItem(page, menuTarget.width / 2, menuTarget.height / 2)
        const hitPath = hitPathAt(page, menuPoint.x, menuPoint.y)
        touchItem(menuTarget)
        let selectedFixture = false
        for (let attempt = 0; attempt < 10 && !selectedFixture; ++attempt) {
            selectedFixture = page.menuRow && page.menuRow.entry
                    && String(page.menuRow.entry.id) === firstFixtureId
            if (!selectedFixture)
                wait(25)
        }
        const selectedId = page.menuRow && page.menuRow.entry
                ? String(page.menuRow.entry.id) : ""
        console.log("PHONE_LIBRARY_MENU " + pageName + " target="
                    + JSON.stringify(rectIn(menuTarget, page))
                    + " hitPath=" + hitPath + " selected=" + selectedFixture
                    + " menuRowId=" + selectedId
                    + " detail=" + detailRequestedSpy.count
                    + " resume=" + resumeRequestedSpy.count)

        // Cleanup only: acceptance is the row selection above and the absence of detail/resume.
        page.closeMenu(false)
        compare(detailRequestedSpy.count, 0,
                pageName + " card-menu touch must not request card details")
        compare(resumeRequestedSpy.count, 0,
                pageName + " card-menu touch must not request resume")
        verify(selectedFixture,
               pageName + " card-menu touch should select its own fixture row")
    }

    function findMouseArea(root) {
        return findMatching(root, function(item) {
            return item.acceptedButtons !== undefined && item.containsMouse !== undefined
        })
    }

    function fixtures(prefix) {
        const rows = []
        for (let i = 0; i < 4; ++i) {
            rows.push({
                "entry": {
                    "id": prefix + "-" + (i + 1),
                    "title": "Fixture " + (i + 1),
                    "cover": "",
                    "type": "movie"
                },
                "progress": 0,
                "state": "unwatched",
                "type": "movie",
                "canResume": false,
                "downloaded": false
            })
        }
        return rows
    }

    function scrolledFixtures(prefix, count) {
        const rows = []
        for (let i = 0; i < count; ++i) {
            rows.push({
                "entry": {
                    "id": prefix + "-scroll-" + (i + 1),
                    "title": "Scrolled Fixture " + (i + 1),
                    "cover": "",
                    "type": "movie"
                },
                "progress": 0,
                "state": "unwatched",
                "type": "movie",
                "canResume": false,
                "downloaded": false
            })
        }
        return rows
    }

    function rectIn(item, ancestor) {
        const point = item.mapToItem(ancestor, 0, 0)
        return { "x": point.x, "y": point.y, "width": item.width, "height": item.height }
    }

    function rectsOverlap(a, b) {
        return a.x < b.x + b.width - 0.5 && b.x < a.x + a.width - 0.5
            && a.y < b.y + b.height - 0.5 && b.y < a.y + a.height - 0.5
    }

    function rectInside(rect, width, height) {
        return rect.x >= -0.5 && rect.y >= -0.5
            && rect.x + rect.width <= width + 0.5
            && rect.y + rect.height <= height + 0.5
    }

    function rectInsideGrid(rect, width, height, requireFullHeight) {
        return rect.x >= -0.5 && rect.x + rect.width <= width + 0.5
            && rect.y >= -0.5 && rect.y < height + 0.5
            && (!requireFullHeight || rect.y + rect.height <= height + 0.5)
    }

    function hitPathAt(root, x, y) {
        let current = root
        const path = []
        for (let depth = 0; current && depth < 12; ++depth) {
            const point = root.mapToItem(current, x, y)
            const child = current.childAt(point.x, point.y)
            if (!child)
                break
            path.push((child.objectName || "<unnamed>")
                      + (child.acceptedButtons !== undefined ? "[MouseArea]" : "[Item]")
                      + "{z=" + child.z + ",visible=" + child.visible + "}")
            current = child
        }
        return path.join(" > ")
    }

    function findLibraryMenuPanel(page, pageName) {
        const names = {
            "Theatre": "theatreLibraryMenuPanel",
            "Biblio": "biblioLibraryMenuPanel",
            "Tankoban": "tankobanLibraryMenuPanel"
        }
        const named = findChild(page, names[pageName])
        if (named)
            return named
        if (pageName === "Biblio") {
            const legacyNamed = findChild(page, "biblioLibraryMenuPanel")
            if (legacyNamed)
                return legacyNamed
        }
        return findMatching(page, function(item) {
            return item.visible && item.width >= 190 && item.width <= 210
                && item.height > 40 && item.height < 400 && item.radius !== undefined
        })
    }

    function touchItem(item) {
        const root = testWindow.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function swipeLeft(flickable) {
        const startX = flickable.width - 14
        const y = flickable.height / 2
        touchEvent(flickable).press(0, flickable, startX, y).commit()
        wait(40)
        for (let step = 1; step <= 4; ++step) {
            const x = Math.round(startX - (startX - 14) * step / 4)
            touchEvent(flickable).move(0, flickable, x, y).commit()
            wait(30)
        }
        touchEvent(flickable).release(0, flickable, 14, y).commit()
        wait(100)
        tryVerify(function() {
            return !flickable.moving && !flickable.dragging && !flickable.flicking
        }, 2000, "filter viewport should finish its committed touch drag/flick")
        console.log("PHONE_LIBRARY_SWIPE contentX=" + flickable.contentX
                    + " max=" + (flickable.contentWidth - flickable.width)
                    + " interactive=" + flickable.interactive)
    }

    function swipeUp(flickable) {
        const x = flickable.width / 2
        const startY = flickable.height - 10
        touchEvent(flickable).press(0, flickable, x, startY).commit()
        wait(40)
        for (let step = 1; step <= 4; ++step) {
            const y = Math.round(startY - (startY - 10) * step / 4)
            touchEvent(flickable).move(0, flickable, x, y).commit()
            wait(30)
        }
        touchEvent(flickable).release(0, flickable, x, 10).commit()
        wait(100)
        tryVerify(function() {
            return !flickable.moving && !flickable.dragging && !flickable.flicking
        }, 2000, "menu viewport should finish its committed touch drag/flick")
    }

    function checkPage(pageName, component, searchName, gridName, filterViewportName,
                       filterButtonName, filterLabel, expectedProperty, expectedValue) {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.androidHost = true
        const page = createTemporaryObject(component, testWindow.contentItem)
        verify(page !== null, pageName + " production page should instantiate")
        if (!page)
            return

        page.active = true
        page.allRows = fixtures(pageName)
        page.visibleRows = fixtures(pageName)
        wait(80)

        const search = findSearch(page, searchName)
        const grid = findGrid(page, gridName)
        const filterViewport = findChild(page, filterViewportName)
        const filterButton = findFilterButton(page, filterButtonName, filterLabel)
        const clickArea = findMouseArea(filterButton)
        verify(search !== null, pageName + " should expose its search field")
        verify(grid !== null, pageName + " should expose its production poster grid")
        verify(filterButton !== null, pageName + " should expose a later sort/filter action")
        verify(clickArea !== null, pageName + " filter action should expose its production pointer target")
        if (!(search && grid && filterButton && clickArea))
            return
        filterPickedSpy.target = filterButton

        const widths = [354, 792, 1280]
        const heights = [792, 354, 800]
        for (let size = 0; size < widths.length; ++size) {
            testWindow.width = widths[size]
            testWindow.height = heights[size]
            testWindow.androidHost = size < 2
            wait(100)
            grid.forceLayout()
            grid.positionViewAtIndex(0, GridView.Beginning)
            wait(40)

            const searchRect = rectIn(search, page)
            verify(search.visible && rectInside(searchRect, page.width, page.height),
                   pageName + " search field should remain inside the visible page at "
                   + widths[size] + "x" + heights[size])

            if (!filterViewport) {
                const buttonRect = rectIn(filterButton, page)
                console.log("PHONE_LIBRARY_GEOMETRY " + pageName + " " + widths[size] + "x"
                            + heights[size] + " filterViewport=missing button="
                            + JSON.stringify(buttonRect))
            }

            tryVerify(function() {
                return grid.itemAtIndex(0) !== null && grid.itemAtIndex(1) !== null
            })
            const first = grid.itemAtIndex(0)
            const second = grid.itemAtIndex(1)
            verify(first !== null && second !== null,
                   pageName + " should instantiate two presentation fixture cards")
            if (first && second) {
                const firstRect = rectIn(first, grid)
                const secondRect = rectIn(second, grid)
                console.log("PHONE_LIBRARY_GEOMETRY " + pageName + " " + widths[size] + "x"
                            + heights[size] + " grid=" + JSON.stringify({
                                "width": grid.width, "height": grid.height
                            }) + " card0=" + JSON.stringify(firstRect)
                            + " card1=" + JSON.stringify(secondRect))
                verify(rectInsideGrid(firstRect, grid.width, grid.height, size === 0),
                       pageName + " first poster must fit inside the grid at " + widths[size]
                       + "x" + heights[size] + "; rect=" + JSON.stringify(firstRect))
                verify(rectInsideGrid(secondRect, grid.width, grid.height, size === 0),
                       pageName + " second poster must fit inside the grid at " + widths[size]
                       + "x" + heights[size] + "; rect=" + JSON.stringify(secondRect))
                verify(!rectsOverlap(firstRect, secondRect),
                       pageName + " poster cards must not overlap at " + widths[size] + "x"
                       + heights[size] + "; card0=" + JSON.stringify(firstRect)
                       + " card1=" + JSON.stringify(secondRect))
            }

            if (filterViewport) {
                const filterRect = rectIn(filterViewport, page)
                verify(filterViewport.visible && filterRect.width > 0 && filterRect.height > 0
                       && rectInside(filterRect, page.width, page.height),
                       pageName + " filter viewport should be visible inside the page at "
                       + widths[size] + "x" + heights[size])
            } else {
                verify(false, pageName + " should expose a bounded filter viewport")
            }

            if (size < 2) {
                if (!filterViewport) {
                    continue
                }
                const targetRectBefore = rectIn(filterButton, filterViewport)
                const overflows = filterViewport.contentWidth > filterViewport.width + 1
                console.log("PHONE_LIBRARY_FILTER " + pageName + " " + widths[size] + "x"
                            + heights[size] + " contentWidth=" + filterViewport.contentWidth
                            + " viewportWidth=" + filterViewport.width + " targetBefore="
                            + JSON.stringify(targetRectBefore))
                let targetRect = targetRectBefore
                for (let swipe = 0; overflows && swipe < 3
                     && !rectInside(targetRect, filterViewport.width, filterViewport.height); ++swipe) {
                    swipeLeft(filterViewport)
                    targetRect = rectIn(filterButton, filterViewport)
                }
                if (overflows)
                    verify(filterViewport.contentX > 0,
                           pageName + " filter overflow should respond to a committed touch swipe")
                verify(filterButton.visible && rectInside(targetRect,
                       filterViewport.width, filterViewport.height),
                       pageName + " later sort/filter should be reachable inside the filter viewport "
                       + "after scrolling; rect=" + JSON.stringify(targetRect))
                filterPickedSpy.clear()
                filterClickedSpy.target = clickArea
                filterClickedSpy.clear()
                const pageTargetRect = rectIn(filterButton, page)
                const pageSearchRect = rectIn(search, page)
                console.log("PHONE_LIBRARY_TARGET " + pageName + " rect="
                            + JSON.stringify(pageTargetRect)
                            + " search=" + JSON.stringify(pageSearchRect)
                            + " viewport=" + JSON.stringify(rectIn(filterViewport, page))
                            + " area=" + JSON.stringify(rectIn(clickArea, filterViewport))
                            + " visible=" + clickArea.visible + " enabled=" + clickArea.enabled
                            + " flickable={moving:" + filterViewport.moving
                            + ",dragging:" + filterViewport.dragging
                            + ",flicking:" + filterViewport.flicking
                            + ",hOvershoot:" + filterViewport.horizontalOvershoot + "}")
                if (expectedProperty === "stateFilter")
                    page.stateFilter = ""
                touchItem(clickArea)
                console.log("PHONE_LIBRARY_HIT " + pageName + " clicked="
                            + filterClickedSpy.count + " picked=" + filterPickedSpy.count
                            + " state=" + page[expectedProperty])
                tryCompare(filterClickedSpy, "count", 1)
                tryCompare(filterPickedSpy, "count", 1)
                tryCompare(page, expectedProperty, expectedValue)
                if (size === 0)
                    checkPhoneCardMenu(page, pageName, expectedProperty)
            }
        }
    }

    function checkScrolledPageMenu(pageName, component, width, height) {
        testWindow.width = width
        testWindow.height = height
        testWindow.androidHost = true
        const flick = createTemporaryObject(pageFlickComponent, testWindow.contentItem)
        verify(flick !== null, pageName + " page-flow fixture should create its page Flickable")
        if (!flick)
            return

        flick.x = 0
        // Match the pageFlick viewport below the phone world chrome.
        flick.y = 140
        flick.width = width
        flick.height = height - 140 - (width > height ? 56 : 72)
        flick.contentHeight = 12000
        flick.contentY = 0
        const page = createTemporaryObject(component, flick.contentItem)
        verify(page !== null, pageName + " page-flow fixture should create its production page")
        if (!page)
            return

        page.width = width
        page.y = 0
        // Other cases cover the real dots hit path. This case isolates viewport
        // geometry: use production page-flow from creation, then open the menu
        // through the page API with a fixture row and its actual dots anchor.
        page.pageFlick = flick
        page.active = false
        const rows = scrolledFixtures(pageName, 48)
        page.allRows = rows
        page.visibleRows = rows
        wait(100)
        page.height = page.flowHeight
        flick.contentHeight = page.flowHeight
        wait(80)

        const grid = findChild(page, pageName === "Theatre" ? "theatreLibraryGrid"
                               : pageName === "Biblio" ? "biblioLibraryGrid"
                               : "tankobanLibraryGrid")
        verify(grid && grid.parent,
               pageName + " should use the real GridView inside the host Flickable")
        if (!(grid && grid.parent))
            return

        const columnCount = Math.max(1, grid.columnCount)
        const targetRow = 0
        const targetIndex = targetRow * columnCount
        const targetId = pageName + "-scroll-" + (targetIndex + 1)
        flick.contentY = 0
        wait(120)
        grid.forceLayout()
        tryVerify(function() { return grid.itemAtIndex(targetIndex) !== null },
                  1500, pageName + " scrolled fixture row should be instantiated")
        const targetMenuName = (pageName === "Theatre"
                                ? "theatreLibraryCardMenu_"
                                : pageName === "Biblio"
                                  ? "biblioLibraryCardMenu_"
                                  : "tankobanLibraryCardMenu_") + targetId
        const menuTarget = findChild(page, targetMenuName)
        verify(menuTarget !== null,
               pageName + " scrolled row should expose its production card-menu button")
        if (!menuTarget)
            return

        let targetRectBefore = rectIn(menuTarget, flick)
        const targetEdgeY = Math.min(flick.height - 64, 100)
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                              flick.contentY + targetRectBefore.y - targetEdgeY))
        wait(100)
        grid.forceLayout()
        targetRectBefore = rectIn(menuTarget, flick)
        const targetRectWindow = rectIn(menuTarget, testWindow.contentItem)
        verify(flick.contentY > 0,
               pageName + " page-flick fixture should exercise nonzero scrolled content")
        verify(menuTarget.visible && menuTarget.opacity >= 0.99
               && rectInside(targetRectBefore, flick.width, flick.height),
               pageName + " scrolled card-menu button should be visible inside the clipped page; rect="
               + JSON.stringify(targetRectBefore))
        verify(rectInside(targetRectWindow, width, height),
               pageName + " scrolled card-menu button should fit in the host window; rect="
               + JSON.stringify(targetRectWindow))
        console.log("PHONE_LIBRARY_FLOW_BEFORE " + pageName + " " + width + "x" + height
                    + " contentY=" + flick.contentY + " viewport="
                    + JSON.stringify({ "x": flick.x, "y": flick.y,
                                        "width": flick.width, "height": flick.height })
                    + " pageFlowHeight=" + page.height + " target="
                    + JSON.stringify(targetRectBefore) + " targetWindow="
                    + JSON.stringify(targetRectWindow))

        detailRequestedSpy.target = page
        resumeRequestedSpy.target = page
        detailRequestedSpy.clear()
        resumeRequestedSpy.clear()
        verify(page.pageFlow,
               pageName + " geometry fixture should use the production page-flow viewport")
        const targetRowData = page.visibleRows[targetIndex]
        const anchor = menuTarget.mapToItem(page, menuTarget.width / 2, menuTarget.height / 2)
        page.openMenu(targetRowData, anchor.x, anchor.y, grid)
        wait(100)
        const rowSelected = page.menuRow && page.menuRow.entry
                && String(page.menuRow.entry.id) === targetId
        const panel = findLibraryMenuPanel(page, pageName)
        const panelVisible = panel && panel.visible
        const panelInFlick = panel ? rectIn(panel, flick) : null
        const panelInWindow = panel ? rectIn(panel, testWindow.contentItem) : null
        const detailCount = detailRequestedSpy.count
        const resumeCount = resumeRequestedSpy.count
        console.log("PHONE_LIBRARY_FLOW_MENU " + pageName + " " + width + "x" + height
                    + " rowSelected=" + rowSelected + " panel="
                    + JSON.stringify(panelInFlick) + " window="
                    + JSON.stringify(panelInWindow) + " detail=" + detailCount
                    + " resume=" + resumeCount)

        verify(rowSelected,
               pageName + " production openMenu API should select the anchored fixture row")
        compare(detailCount, 0, pageName + " geometry fixture must not request card details")
        compare(resumeCount, 0, pageName + " geometry fixture must not request resume")
        verify(panel !== null && panelVisible,
               pageName + " open-menu state should expose its production menu panel")
        if (panelInFlick)
            verify(rectInside(panelInFlick, flick.width, flick.height),
                   pageName + " complete menu should fit inside the clipped page viewport; rect="
                   + JSON.stringify(panelInFlick))
        if (panelInFlick) {
            verify(panelInFlick.x >= 8 && panelInFlick.y >= 8
                   && panelInFlick.x + panelInFlick.width <= flick.width - 8
                   && panelInFlick.y + panelInFlick.height <= flick.height - 8,
                   pageName + " menu should retain 8dp margins inside the phone page viewport; rect="
                   + JSON.stringify(panelInFlick) + " viewport="
                   + JSON.stringify({ "width": flick.width, "height": flick.height }))
        }
        if (panelInWindow)
            verify(rectInside(panelInWindow, width, height),
                   pageName + " complete menu should fit inside the phone window; rect="
                   + JSON.stringify(panelInWindow))

        if (pageName === "Theatre" && width === 792 && panel) {
            const menuScroll = findChild(page, "theatreLibraryMenuScroll")
            const lastAction = findMatching(page, function(item) {
                return item.actionId === "remove"
            })
            verify(menuScroll !== null && lastAction !== null,
                   "Theatre short-landscape menu should expose its production scroll and last action")
            if (menuScroll && lastAction) {
                console.log("PHONE_LIBRARY_THEATRE_MENU_SCROLL before content="
                            + menuScroll.contentHeight + " viewport=" + menuScroll.height
                            + " last=" + JSON.stringify(rectIn(lastAction, menuScroll)))
                verify(menuScroll.clip && menuScroll.interactive
                       && menuScroll.contentHeight > menuScroll.height,
                       "Theatre short-landscape menu should scroll its overflow within the clip")
                verify(lastAction.height >= 48,
                       "Theatre final menu action should retain a 48dp touch row")
                swipeUp(menuScroll)
                // The phone menu can gain actions (for example Select). Keep using
                // ordinary swipes until its final action is fully exposed, with a bound.
                for (let swipe = 0; swipe < 4
                        && !rectInside(rectIn(lastAction, menuScroll), menuScroll.width, menuScroll.height); ++swipe)
                    swipeUp(menuScroll)
                const lastActionRect = rectIn(lastAction, menuScroll)
                console.log("PHONE_LIBRARY_THEATRE_MENU_SCROLL after contentY="
                            + menuScroll.contentY + " last=" + JSON.stringify(lastActionRect)
                            + " viewport=" + JSON.stringify({ "width": menuScroll.width,
                                                               "height": menuScroll.height }))
                verify(menuScroll.contentY > 0,
                       "Theatre short-landscape menu should respond to a committed vertical touch swipe")
                verify(rectInside(lastActionRect, menuScroll.width, menuScroll.height),
                       "Theatre final 48dp menu action should become fully visible inside its clip; rect="
                       + JSON.stringify(lastActionRect))
            }
        }
        // Fixture cleanup happens after asserting the visible scrolled menu state.
        if (page.closeMenu)
            page.closeMenu(false)
        flick.destroy()
    }

    function test_theatre_library_phone_portrait_landscape_and_desktop() {
        checkPage("Theatre", theatrePageComponent, "theatreLibrarySearch", "theatreLibraryGrid",
                  "theatreLibraryFilterViewport", "", "Ended", "airingFilter", "ended")
    }

    function test_theatre_portrait_phone_density_and_long_title_geometry() {
        const longTitle = "A Very Long Theatre Feature Title That Must Wrap Cleanly"
        testWindow.width = 318
        testWindow.height = 792
        testWindow.androidHost = true
        const page = createTemporaryObject(theatrePageComponent, testWindow.contentItem)
        verify(page !== null, "Theatre production page should instantiate at the narrow portrait width")
        if (!page)
            return

        page.active = true
        const rows = fixtures("TheatreDensity")
        rows[0].entry.title = longTitle
        page.allRows = rows
        page.visibleRows = rows
        wait(100)

        const grid = findChild(page, "theatreLibraryGrid")
        verify(grid !== null, "Theatre production grid should remain discoverable")
        if (!grid)
            return
        grid.forceLayout()
        tryVerify(function() {
            return grid.itemAtIndex(0) !== null && grid.itemAtIndex(1) !== null
                && grid.itemAtIndex(2) !== null
        }, 1500, "first Theatre row should instantiate three production cards")

        const first = grid.itemAtIndex(0)
        const second = grid.itemAtIndex(1)
        const third = grid.itemAtIndex(2)
        verify(first && second && third, "three Theatre cards should render in the portrait viewport")
        if (first && second && third) {
            compare(grid.columnCount, 3,
                    "318dp portrait phone width should use three Theatre poster columns")
            verify(grid.posterW >= 90 && grid.posterW <= 96,
                   "portrait Theatre posters should stay near 94dp wide; posterW=" + grid.posterW)
            const firstRect = rectIn(first, grid)
            const secondRect = rectIn(second, grid)
            const thirdRect = rectIn(third, grid)
            verify(Math.abs(firstRect.y - secondRect.y) < 0.5
                   && Math.abs(secondRect.y - thirdRect.y) < 0.5,
                   "the first three Theatre cards should share one rendered row")
            verify(firstRect.x < secondRect.x && secondRect.x < thirdRect.x
                   && thirdRect.x + thirdRect.width <= grid.width + 0.5,
                   "all three Theatre cards should fit without horizontal clipping")

            const title = findMatching(first, function(item) {
                return item.text !== undefined && String(item.text) === longTitle
                    && item.font && item.font.pixelSize === 13
            })
            const metadata = findMatching(first, function(item) {
                return item.text !== undefined && String(item.text) === "Movie"
                    && item.font && item.font.pixelSize === 12
            })
            verify(title !== null, "portrait card should render its separate 13px title label")
            verify(metadata !== null, "portrait card should render its separate metadata label")
            if (title && metadata) {
                verify(title.maximumLineCount >= 2 && title.wrapMode === Text.WordWrap,
                       "portrait title should be allowed up to two readable wrapped lines")
                verify(title.width >= 90 && title.width <= 96 && title.height >= 26,
                       "long title should have a readable two-line text box; size="
                       + title.width + "x" + title.height)
                const titleRect = rectIn(title, first)
                const metadataRect = rectIn(metadata, first)
                verify(titleRect.y + titleRect.height <= metadataRect.y + 0.5,
                       "metadata should start after the full title box; title="
                       + JSON.stringify(titleRect) + " metadata=" + JSON.stringify(metadataRect))
            }

            const menu = findChild(page, "theatreLibraryCardMenu_TheatreDensity-1")
            verify(menu !== null && menu.width >= 48 && menu.height >= 48,
                   "portrait poster menu should preserve its 48dp touch target")
        }

        const portraitColumns = grid.columnCount
        testWindow.width = 190
        wait(100)
        grid.forceLayout()
        compare(grid.columnCount, 1,
                "narrow portrait phone should fall back to one visible poster column")
        const narrowCard = grid.itemAtIndex(0)
        verify(narrowCard !== null && narrowCard.x >= 0
               && narrowCard.x + narrowCard.width <= grid.width + 0.5,
               "narrow fallback poster should remain fully inside its grid")

        testWindow.width = 792
        testWindow.height = 354
        wait(100)
        grid.forceLayout()
        compare(grid.columnCount, Math.max(1, Math.floor(grid.width / 128)),
                "short landscape phone should retain its previous phone column rule")
        compare(grid.posterW, Math.min(148, Math.max(0, grid.cellWidth - 16)),
                "short landscape phone should retain its previous poster sizing")

        testWindow.width = 1280
        testWindow.height = 800
        testWindow.androidHost = false
        wait(100)
        grid.forceLayout()
        compare(grid.posterW, 148, "desktop poster width should remain unchanged")
        verify(portraitColumns === 3,
               "portrait density should be specific to phone dimensions")
        page.destroy()

        testWindow.width = 792
        testWindow.height = 354
        testWindow.androidHost = true
        const landscapeFlick = createTemporaryObject(pageFlickComponent, testWindow.contentItem)
        verify(landscapeFlick !== null, "landscape page-flow viewport should instantiate")
        if (!landscapeFlick)
            return
        landscapeFlick.x = 0
        landscapeFlick.y = 0
        landscapeFlick.width = 792
        landscapeFlick.height = 354
        landscapeFlick.contentHeight = 1800
        const tallPage = createTemporaryObject(theatreFlowPageComponent, landscapeFlick.contentItem)
        verify(tallPage !== null, "tall Theatre page-flow root should instantiate")
        if (!tallPage) {
            landscapeFlick.destroy()
            return
        }
        tallPage.x = 0
        tallPage.y = 0
        tallPage.width = 756
        tallPage.height = 1800
        tallPage.pageFlick = landscapeFlick
        tallPage.active = true
        tallPage.allRows = fixtures("TheatreTallLandscape")
        tallPage.visibleRows = fixtures("TheatreTallLandscape")
        wait(100)

        const tallGrid = findChild(tallPage, "theatreLibraryGrid")
        verify(tallGrid !== null, "tall landscape Theatre page should expose its production grid")
        verify(tallPage.height > testWindow.height,
               "landscape page-flow root should be taller than its owning window")
        verify(!tallPage.portraitPhoneLayout,
               "portrait density must follow the 792x354 owning window, not tall content bounds")
        if (tallGrid) {
            tallGrid.forceLayout()
            compare(tallGrid.columnCount, Math.max(1, Math.floor(tallGrid.width / 128)),
                    "tall landscape page should retain the previous landscape column rule")
            compare(tallGrid.posterW, Math.min(148, Math.max(0, tallGrid.cellWidth - 16)),
                    "tall landscape page should retain the previous landscape poster sizing")
        }
        landscapeFlick.destroy()
    }

    function test_biblio_library_phone_portrait_landscape_and_desktop() {
        checkPage("Biblio", biblioPageComponent, "biblioLibrarySearch", "biblioLibraryGrid",
                  "biblioLibraryFilterViewport", "", "In Progress", "stateFilter", "inProgress")
    }

    function test_tankoban_library_phone_portrait_landscape_and_desktop() {
        checkPage("Tankoban", tankobanPageComponent, "tankobanLibrarySearch", "tankobanLibraryGrid",
                  "tankobanLibraryFilterViewport", "", "A-Z", "sortMode", "az")
    }

    function test_scrolled_library_menus_fit_page_viewport_and_phone_window() {
        const pages = [
            { "name": "Theatre", "component": theatreFlowPageComponent },
            { "name": "Biblio", "component": biblioFlowPageComponent },
            { "name": "Tankoban", "component": tankobanFlowPageComponent }
        ]
        const widths = [354, 792]
        const heights = [792, 354]
        for (let pageIndex = 0; pageIndex < pages.length; ++pageIndex) {
            for (let size = 0; size < widths.length; ++size)
                checkScrolledPageMenu(pages[pageIndex].name, pages[pageIndex].component,
                                      widths[size], heights[size])
        }
    }
}

import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneCrossworldLibraries"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        property bool androidHost: true
        visible: true
    }

    Component { id: biblioPageComponent; App.BiblioLibraryPage { anchors.fill: parent } }
    Component { id: tankobanPageComponent; App.TankobanLibraryTab { anchors.fill: parent } }
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

    function fixtures(prefix) {
        const rows = []
        for (let i = 0; i < 6; ++i) {
            const type = i % 2 === 0 ? "manga" : "comic"
            rows.push({
                "entry": {
                    "id": prefix + "-" + (i + 1),
                    "title": "A Very Long Fixture Title That Should Wrap onto Two Lines",
                    "cover": "",
                    "type": type
                },
                "author": "Fixture Author",
                "progress": 0.5,
                "state": "inProgress",
                "mediaType": type,
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

    function findGrid(page, objectName) {
        return findChild(page, objectName)
    }

    function checkPortraitGrid(pageName, component, gridName, menuName, expectsAuthor) {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.androidHost = true
        const page = createTemporaryObject(component, testWindow.contentItem)
        verify(page !== null, pageName + " production page should instantiate")
        if (!page)
            return null

        page.active = true
        const rows = fixtures(pageName)
        page.allRows = rows
        page.visibleRows = rows
        wait(100)

        const grid = findGrid(page, gridName)
        verify(grid !== null, pageName + " production grid should be discoverable")
        if (!grid)
            return page

        for (const width of [354, 318]) {
            testWindow.width = width
            wait(100)
            grid.forceLayout()
            tryVerify(function() {
                return grid.itemAtIndex(0) !== null && grid.itemAtIndex(1) !== null
                    && grid.itemAtIndex(2) !== null
            }, 1500, pageName + " should realize three portrait poster cards")
            compare(page.portraitPhoneLayout, true,
                    pageName + " portrait density should follow the owning Android window")
            compare(grid.columnCount, 3,
                    pageName + " should use three columns at " + width + "dp portrait width")
            verify(grid.posterW >= 90 && grid.posterW <= 96,
                   pageName + " portrait posters should stay near 94dp; posterW=" + grid.posterW)

            const first = grid.itemAtIndex(0)
            const second = grid.itemAtIndex(1)
            const third = grid.itemAtIndex(2)
            verify(first && second && third,
                   pageName + " should render three cards in the first portrait row")
            if (first && second && third) {
                const firstRect = rectIn(first, grid)
                const secondRect = rectIn(second, grid)
                const thirdRect = rectIn(third, grid)
                verify(Math.abs(firstRect.y - secondRect.y) < 0.5
                       && Math.abs(secondRect.y - thirdRect.y) < 0.5,
                       pageName + " first three cards should share one rendered row")
                verify(firstRect.x < secondRect.x && secondRect.x < thirdRect.x
                       && thirdRect.x + thirdRect.width <= grid.width + 0.5,
                       pageName + " three cards should fit without horizontal clipping")

                const title = findMatching(first, function(item) {
                    return item.text !== undefined
                        && String(item.text) === rows[0].entry.title
                        && item.font && item.font.pixelSize === 13
                })
                verify(title !== null,
                       pageName + " portrait card should render a 13px title")
                if (title) {
                    verify(title.maximumLineCount >= 2 && title.wrapMode === Text.WordWrap,
                           pageName + " portrait title should wrap across two readable lines")
                    verify(title.width >= 90 && title.width <= 96 && title.height >= 26,
                           pageName + " portrait title should have a readable two-line box; size="
                           + title.width + "x" + title.height)
                }

                if (expectsAuthor) {
                    const author = findMatching(first, function(item) {
                        return item.text !== undefined && String(item.text) === "Fixture Author"
                            && item.font && item.font.pixelSize === 12
                    })
                    verify(author !== null, "Biblio should retain its author metadata")
                    if (author && title) {
                        const titleRect = rectIn(title, first)
                        const authorRect = rectIn(author, first)
                        verify(authorRect.y >= titleRect.y + titleRect.height - 0.5,
                               "Biblio author metadata should begin after the two-line title; title="
                               + JSON.stringify(titleRect) + " author=" + JSON.stringify(authorRect))
                    }
                }
            }

            const menu = findChild(page, menuName)
            verify(menu !== null && menu.width >= 48 && menu.height >= 48,
                   pageName + " portrait card menu should retain its 48dp touch target")
        }

        testWindow.width = 190
        wait(100)
        grid.forceLayout()
        compare(grid.columnCount, 1, pageName + " narrow portrait should retain a one-column fallback")
        verify(grid.itemAtIndex(0) !== null
               && grid.itemAtIndex(0).x >= 0
               && grid.itemAtIndex(0).x + grid.itemAtIndex(0).width <= grid.width + 0.5,
               pageName + " narrow fallback card should remain inside the grid")

        testWindow.width = 792
        testWindow.height = 354
        wait(100)
        grid.forceLayout()
        compare(page.portraitPhoneLayout, false,
                pageName + " landscape sizing should follow the owning window orientation")
        compare(grid.columnCount, Math.max(1, Math.floor(grid.width / 128)),
                pageName + " landscape phone should retain its previous column rule")
        compare(grid.posterW, Math.min(148, Math.max(0, grid.cellWidth - 16)),
                pageName + " landscape phone should retain its previous poster width")

        const desktopSizes = [
            { "width": 1280, "height": 800, "android": false },
            { "width": 800, "height": 1280, "android": true }
        ]
        for (const size of desktopSizes) {
            testWindow.width = size.width
            testWindow.height = size.height
            testWindow.androidHost = size.android
            wait(100)
            grid.forceLayout()
            verify(!page.phoneLayout,
                   pageName + " tablet/desktop dimensions should keep the existing layout")
            if (pageName === "Biblio") {
                compare(grid.columnCount, Math.max(2, Math.floor((grid.width + 20) / 168)),
                        "Biblio tablet/desktop should preserve its previous column rule")
                compare(grid.posterW, 148, "Biblio tablet/desktop should preserve 148dp posters")
            } else {
                const columns = Math.max(2, Math.floor(grid.width / 178))
                compare(grid.columnCount, columns,
                        "Tankoban tablet/desktop should preserve its previous column rule")
                compare(grid.posterW, Math.max(0, grid.cellWidth - 16),
                        "Tankoban tablet/desktop should preserve its previous card width")
            }
        }

        page.destroy()
        return null
    }

    function test_biblio_library_portrait_density_and_geometry() {
        checkPortraitGrid("Biblio", biblioPageComponent, "biblioLibraryGrid",
                         "biblioLibraryCardMenu_Biblio-1", true)
    }

    function test_tankoban_library_portrait_density_and_geometry() {
        checkPortraitGrid("Tankoban", tankobanPageComponent, "tankobanLibraryGrid",
                         "tankobanLibraryCardMenu_Tankoban-1", false)
    }

    function test_tall_flow_roots_follow_landscape_window_orientation() {
        testWindow.width = 792
        testWindow.height = 354
        testWindow.androidHost = true
        const cases = [
            { "name": "Biblio", "component": biblioFlowPageComponent, "grid": "biblioLibraryGrid" },
            { "name": "Tankoban", "component": tankobanFlowPageComponent, "grid": "tankobanLibraryGrid" }
        ]
        for (const current of cases) {
            const flick = createTemporaryObject(pageFlickComponent, testWindow.contentItem)
            verify(flick !== null, current.name + " landscape page viewport should instantiate")
            if (!flick)
                continue
            flick.x = 0
            flick.y = 0
            flick.width = 792
            flick.height = 354
            const page = createTemporaryObject(current.component, flick.contentItem)
            verify(page !== null, current.name + " tall page root should instantiate")
            if (!page) {
                flick.destroy()
                continue
            }
            page.x = 0
            page.y = 0
            page.width = 756
            page.height = 1800
            page.pageFlick = flick
            page.active = true
            page.allRows = fixtures(current.name + "Tall")
            page.visibleRows = page.allRows
            wait(100)
            const grid = findChild(page, current.grid)
            verify(grid !== null && page.height > testWindow.height,
                   current.name + " test should exercise a tall production page root")
            compare(page.portraitPhoneLayout, false,
                    current.name + " portrait density should follow the landscape owning window")
            if (grid) {
                grid.forceLayout()
                compare(grid.columnCount, Math.max(1, Math.floor(grid.width / 128)),
                        current.name + " tall landscape root should keep landscape columns")
                compare(grid.posterW, Math.min(148, Math.max(0, grid.cellWidth - 16)),
                        current.name + " tall landscape root should keep landscape poster width")
            }
            flick.destroy()
        }
    }
}

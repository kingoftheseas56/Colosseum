import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "WallSingleScroller"
    when: windowShown

    Window {
        id: testWindow
        width: 1000; height: 650; visible: true
        Flickable {
            id: pageFlick
            width: parent.width; height: parent.height
            contentWidth: width
            clip: true
        }
    }

    Component { id: discoverComponent; Colosseum.DiscoverBrowser { width: 960; height: flowHeight; posterVisualProfile: "gallery" } }
    Component { id: theatreComponent; Colosseum.LibraryPage { width: 960; height: flowHeight } }
    Component { id: tankobanComponent; Colosseum.TankobanLibraryTab { width: 960; height: flowHeight } }
    Component { id: biblioComponent; Colosseum.BiblioLibraryPage { width: 960; height: flowHeight } }

    property var page: null
    property int fetchCount: 0

    function makeRows(count, world) {
        var rows = []
        for (var i = 0; i < count; ++i)
            rows.push({ entry: { id: world + i, title: "Title " + i, author: "Author" },
                        state: "unwatched", progress: 0, canResume: false,
                        downloaded: false, newCount: 0 })
        return rows
    }
    function makeCards(start, count) {
        var cards = []
        for (var i = start; i < start + count; ++i)
            cards.push({ id: "card-" + i, title: "Title " + i, type: "movie", cover: "" })
        return cards
    }
    function descendants(item, out) {
        if (!item || out.indexOf(item) >= 0) return
        out.push(item)
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) descendants(children[i], out)
        if (item.contentItem && item.contentItem !== item) descendants(item.contentItem, out)
    }
    function wallOf(item) {
        var all = []; descendants(item, all)
        for (var i = 0; i < all.length; ++i)
            if (all[i].cellHeight !== undefined && all[i].contentY !== undefined)
                return all[i]
        return null
    }
    function assertSingleScroller(item) {
        var all = []; descendants(item, all)
        for (var i = 0; i < all.length; ++i) {
            var child = all[i]
            if (child.interactive !== undefined && child.contentHeight !== undefined)
                verify(!child.interactive || child.contentHeight <= child.height + 1,
                       "nested vertical scroller: " + String(child.objectName || child))
        }
    }
    function countCards(wall) {
        var all = []; descendants(wall.contentItem, all)
        var count = 0
        for (var i = 0; i < all.length; ++i)
            if (String(all[i].objectName || "").indexOf("discoverCard_") === 0
                    || String(all[i].objectName || "").indexOf("biblioLibraryCard_") === 0)
                ++count
        return count
    }
    function countDelegateHosts(wall) {
        var all = []; descendants(wall.contentItem, all)
        var count = 0
        for (var i = 0; i < all.length; ++i)
            if (all[i].modelData !== undefined && all[i].index !== undefined)
                ++count
        return count
    }
    function init() {
        fetchCount = 0
        pageFlick.contentY = 0
        pageFlick.contentHeight = 0
    }
    function cleanup() {
        if (page) page.destroy()
        page = null
        pageFlick.contentY = 0
        pageFlick.contentHeight = 0
    }
    function createPage(component, world) {
        page = component.createObject(pageFlick.contentItem, { pageFlick: pageFlick })
        verify(page !== null)
        if (world !== "discover") page.visibleRows = makeRows(600, world)
        pageFlick.contentHeight = page.flowHeight + 100
        wait(80)
        waitForRendering(page)
        compare(page.height, page.flowHeight)
        var wall = wallOf(page)
        verify(wall !== null)
        assertSingleScroller(page)
        verify(countDelegateHosts(wall) > 0, world + " must render cards")
        verify(countDelegateHosts(wall) < 60, world + " must virtualize 600 cards")
        var maxY = pageFlick.contentHeight - pageFlick.height
        pageFlick.contentY = Math.min(maxY, Math.max(0, page.flowHeight / 2))
        wait(80)
        var expected = Math.max(0, Math.min(pageFlick.contentY - wall.mapToItem(pageFlick.contentItem, 0, 0).y
                         + wall.y, wall.contentHeight - wall.height))
        fuzzyCompare(wall.contentY - wall.originY, expected, 2)
        return wall
    }
    function test_theatre_library_flows_and_virtualizes() { createPage(theatreComponent, "theatre") }
    function test_tankoban_library_flows_and_virtualizes() { createPage(tankobanComponent, "tankoban") }
    function test_biblio_library_flows_and_virtualizes() {
        var wall = createPage(biblioComponent, "biblio")
        verify(countCards(wall) > 0, "the book grid must have rendered cards")
        verify(countCards(wall) < 60, "600 books must not materialize 60 cards")
    }
    function test_discover_pages_and_virtualizes() {
        page = discoverComponent.createObject(pageFlick.contentItem, { pageFlick: pageFlick, active: false })
        verify(page !== null)
        page.adapter = {
            types: function() { return [{ key: "movie", label: "Movies" }] },
            catalogs: function() { return [{ key: "builtin", label: "Built-in" }] },
            defaultCatalog: function() { return "builtin" },
            filters: function() { return [] },
            fetchPage: function(state, cursor, gen, done) {
                ++testCase.fetchCount
                Qt.callLater(function() {
                    done(gen, { items: testCase.makeCards(cursor ? 60 : 0, 60),
                                nextCursor: cursor ? null : "more", exhausted: !!cursor })
                })
            }
        }
        page.active = true
        tryCompare(page, "initialized", true)
        tryCompare(page, "loading", false)
        waitForRendering(page)
        var wall = wallOf(page)
        verify(wall !== null)
        tryCompare(wall, "count", 60)
        pageFlick.contentHeight = page.flowHeight + 100
        compare(page.height, page.flowHeight)
        assertSingleScroller(page)
        compare(fetchCount, 1)
        tryVerify(function() { return wall.contentHeight > wall.height }, 1000)
        pageFlick.contentY = Math.max(0, Math.min(pageFlick.contentHeight - pageFlick.height,
            wall.parent.mapToItem(pageFlick.contentItem, 0, 0).y
            + wall.contentHeight - wall.height - 2 * wall.cellHeight + 2))
        tryVerify(function() { return fetchCount >= 2 }, 1000,
                  "the next page should be requested within two rows of the end")
        tryCompare(page, "loading", false)
        page.destroy()
        pageFlick.contentY = 0
        page = discoverComponent.createObject(pageFlick.contentItem, { pageFlick: pageFlick, active: false })
        verify(page !== null)
        page.initialized = true
        page.exhausted = true
        page.items = makeCards(0, 600)
        page.active = true
        pageFlick.contentHeight = page.flowHeight + 100
        wall = wallOf(page)
        waitForRendering(page)
        tryVerify(function() { return countCards(wall) > 0 }, 1000)
        verify(countCards(wall) < 60, "600 discover cards must stay virtualized")
        pageFlick.contentY = Math.max(0, pageFlick.contentHeight / 2)
        wait(80)
        fuzzyCompare(wall.contentY - wall.originY,
                     Math.max(0, Math.min(pageFlick.contentY
                         - wall.parent.mapToItem(pageFlick.contentItem, 0, 0).y,
                         wall.contentHeight - wall.height)), 2)
    }
}

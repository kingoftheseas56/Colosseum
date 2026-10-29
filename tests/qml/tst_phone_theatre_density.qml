import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneTheatreDensity"
    when: windowShown

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true

        Item {
            id: board
            x: host.width < 600 ? 18 : 0
            width: host.width < 600 ? host.width - 36 : host.width
            height: host.height
        }
    }

    Component {
        id: discoverComponent
        App.DiscoverBrowser { anchors.fill: parent }
    }

    Component {
        id: theatreFixtureComponent
        Item {
            id: fixtureRoot
            width: board.width
            height: board.height
            property var loader: null
            property bool loadActive: false
            property alias page: theatrePage

            Flickable {
                id: pageFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: Math.max(height, pageColumn.implicitHeight + 50)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: pageColumn
                    width: pageFlick.width

                    App.TheatreCatalogPage {
                        id: theatrePage
                        pageKey: "movies"
                        visualProfile: "gallery"
                        active: fixtureRoot.loadActive
                        catalogLoader: fixtureRoot.loader
                        pageFlick: pageFlick
                        viewportTop: pageFlick.contentY
                        viewportHeight: pageFlick.height
                        height: implicitHeight
                    }
                }
            }
        }
    }

    property var browser: null
    property var theatreHost: null
    property var pendingCataloguePush: null
    property int pendingCatalogueGeneration: -1
    property bool deferCatalogueResponse: false

    function makeCards(count) {
        const cards = []
        for (let i = 0; i < count; ++i) {
            cards.push({ id: "local-movie-" + i, type: "movie",
                         title: "Local Movie " + i, cover: "", year: 2025 })
        }
        return cards
    }

    function makeDiscoverAdapter() {
        return {
            types: function() { return [{ key: "movie", label: "Movies" }] },
            catalogs: function() {
                return [{ key: "fixture-local", title: "Fixture", sourceKind: "house",
                          section: "Fixture Source", sourceName: "Fixture Source" }]
            },
            defaultCatalog: function() { return "fixture-local" },
            filters: function() {
                return [{ group: "Genre", options: [{ key: "all", label: "All genres" }] }]
            },
            fetchPage: function(state, cursor, generation, done) {
                done(generation, { items: testCase.makeCards(9), nextCursor: null,
                                   exhausted: true, freshness: "", warning: "" })
            }
        }
    }

    function init() {
        host.width = 354
        host.height = 792
        host.androidHost = true
        deferCatalogueResponse = false
        pendingCataloguePush = null
        pendingCatalogueGeneration = -1
        wait(20)
    }

    function cleanup() {
        if (browser)
            browser.destroy()
        browser = null
        if (theatreHost)
            theatreHost.destroy()
        theatreHost = null
        pendingCataloguePush = null
        wait(0)
    }

    function setViewport(width, height) {
        host.width = width
        host.height = height
        host.androidHost = true
        wait(50)
    }

    function createDiscover() {
        browser = discoverComponent.createObject(board, {
            automationPrefix: "fixture",
            adapter: makeDiscoverAdapter(),
            posterVisualProfile: "gallery"
        })
        verify(browser !== null, "the actual DiscoverBrowser instantiates with a local adapter")
        // The compact phone treatment is opt-in. Do not assign an unknown QML property on baseline.
        if (browser.compactPhonePosters !== undefined)
            browser.compactPhonePosters = true
        tryCompare(browser, "initialized", true)
        tryCompare(browser, "loading", false)
        wait(60)
        waitForRendering(browser)
        return browser
    }

    function makeShelfItems(prefix, count) {
        const items = []
        for (let i = 0; i < count; ++i) {
            items.push({ id: prefix + "-" + i, title: prefix + " title " + i,
                         type: "movie", cover: "", year: 2025 })
        }
        return items
    }

    function makeCatalogueRows() {
        return [
            { key: "top-10", title: "Top 10", placement: 0, ranked: true,
              sourceKind: "house", sourceLabel: "Local fixture",
              items: makeShelfItems("ranked", 10),
              seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: "top-10" } },
            { key: "popular", title: "Popular", placement: 1, ranked: false,
              sourceKind: "house", sourceLabel: "Local fixture",
              items: makeShelfItems("ordinary", 9),
              seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: "popular" } }
        ]
    }

    function localCatalogueLoader(pageKey, options, push) {
        if (testCase.deferCatalogueResponse) {
            testCase.pendingCataloguePush = push
            testCase.pendingCatalogueGeneration = options.generation
            return null
        }
        push({ pageKey: pageKey, generation: options.generation,
               rows: testCase.makeCatalogueRows(), loading: false, error: "" })
        return null
    }

    function createTheatre(deferResponse) {
        deferCatalogueResponse = !!deferResponse
        theatreHost = theatreFixtureComponent.createObject(board, {
            loader: testCase.localCatalogueLoader,
            loadActive: false
        })
        verify(theatreHost !== null, "the actual TheatreCatalogPage fixture container instantiates")
        const page = theatreHost.page
        verify(page !== null)
        if (page.compactPhonePosters !== undefined)
            page.compactPhonePosters = true
        theatreHost.loadActive = true
        wait(80)
        return page
    }

    function allDescendants(root, result) {
        if (!root || result.indexOf(root) >= 0)
            return
        result.push(root)
        const children = root.children || []
        for (let i = 0; i < children.length; ++i)
            allDescendants(children[i], result)
        if (root.contentItem && root.contentItem !== root)
            allDescendants(root.contentItem, result)
        if (root.item && root.item !== root)
            allDescendants(root.item, result)
    }

    function findChild(root, objectName) {
        const all = []
        allDescendants(root, all)
        for (let i = 0; i < all.length; ++i)
            if (all[i].objectName === objectName)
                return all[i]
        return null
    }

    function findDescendant(root, predicate) {
        const all = []
        allDescendants(root, all)
        for (let i = 0; i < all.length; ++i)
            if (predicate(all[i]))
                return all[i]
        return null
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.y >= 0
               && point.x <= root.width && point.y <= root.height,
               "the touched control is inside the visible host")
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(300)
    }

    function shelfForKey(page, key) {
        const index = page.mainRows.findIndex(function(row) { return row.key === key })
        return index >= 0 ? page.mainShelfAt(index) : null
    }

    function railInShelf(shelf) {
        return findDescendant(shelf, function(item) {
            return item.posterWidth !== undefined && item.posterHeight !== undefined
                    && item.visibleItems !== undefined && item.currentContentX !== undefined
        })
    }

    function listInRail(rail) {
        return findDescendant(rail, function(item) {
            return item.orientation === ListView.Horizontal && item.count !== undefined
                    && item.contentWidth !== undefined && item.itemAtIndex !== undefined
        })
    }

    function assertPortraitRail(page, key, ranked) {
        const shelf = shelfForKey(page, key)
        verify(shelf !== null, "the actual " + key + " LazyPosterShelf is present")
        tryCompare(shelf, "railLoaded", true)
        const rail = railInShelf(shelf)
        verify(rail !== null, "the actual " + key + " PosterRail is mounted")
        compare(rail.ranked, ranked)
        compare(rail.width, 318)
        compare(rail.posterWidth, 98)
        compare(rail.posterHeight, 147)
        compare(rail.cardGap, 12)
        compare(rail.compactLayout, true)
        const list = listInRail(rail)
        verify(list !== null, "the actual " + key + " poster ListView is present")
        compare(list.count, ranked ? 10 : 9)
        tryVerify(function() { return list.itemAtIndex(2) !== null }, 2000)
        for (let i = 0; i < 3; ++i) {
            const card = list.itemAtIndex(i)
            verify(card !== null, key + " card " + i + " is instantiated")
            const position = card.mapToItem(list, 0, 0)
            verify(position.x >= -1 && position.x + card.width <= list.width + 1,
                   key + " card " + i + " stays inside the visible rail")
            verify(card.width >= 84 && card.width <= 106,
                   key + " card art remains readable at " + card.width + "dp")
        }
        verify(Math.abs(shelf.reservedHeight - rail.implicitHeight) <= 1,
               key + " lazy reservation equals the mounted rail height")
        return { shelf: shelf, rail: rail, list: list }
    }

    function destroyFixtures() {
        if (browser)
            browser.destroy()
        browser = null
        if (theatreHost)
            theatreHost.destroy()
        theatreHost = null
        wait(0)
    }

    function test_discover_phone_portrait_uses_full_board_and_three_compact_columns() {
        createDiscover()
        compare(browser.width, 318)
        verify(browser.phoneNarrow,
               "the test Window androidHost flag activates DiscoverBrowser phone rules")
        tryCompare(browser, "initialized", true)
        tryCompare(browser, "loading", false)
        compare(browser.items.length, 9,
                "the wall is populated only from local fixture rows")
        waitForRendering(browser)

        compare(browser.contentLeft, 0,
                "the phone Sources rail overlays the full-width wall instead of reserving 64dp")
        compare(browser.width, 318)
        compare(browser._galleryColumnCountForTest, 3,
                "the 318dp portrait wall fits three readable gallery columns")
        verify(browser._galleryDelegateWidthForTest >= 84
               && browser._galleryDelegateWidthForTest <= 106,
               "phone gallery poster delegates remain within the readable 84–106dp range")
    }

    function test_discover_narrow_phone_keeps_three_readable_columns() {
        setViewport(320, 792)
        const actual = createDiscover()
        verify(actual.phoneNarrow)
        verify(actual.compactPhonePortrait)
        compare(actual.width, 284,
                "the 320dp Android host leaves the fixture's 18dp side insets")
        compare(actual.contentLeft, 0,
                "the portrait Sources rail overlays the narrow wall")
        compare(actual._galleryColumnCountForTest, 3,
                "even the 284dp wall should retain three compact phone columns")
        verify(actual._galleryDelegateWidthForTest >= 84
               && actual._galleryDelegateWidthForTest <= 100,
               "narrow-phone poster art stays 84–100dp instead of stretching")
    }

    function test_discover_sources_action_overlays_without_squeezing_wall() {
        const actual = createDiscover()
        const action = findChild(actual, "fixtureDiscoverSourcesAction")
        const button = findChild(actual, "fixtureDiscoverSourcesButton")
        const sidebar = findChild(actual, "fixtureDiscoverSidebar")
        const wall = findChild(actual, "fixtureDiscoverWall")
        const collapse = findChild(actual, "fixtureDiscoverSidebarCollapse")
        verify(action && button && sidebar && wall && collapse,
               "the real Sources action, sidebar, wall, and close target are present")
        verify(button.width >= 48 && button.height >= 48,
               "Sources has a phone-sized visible button")
        verify(action.width >= 48 && action.height >= 48,
               "Sources has a phone-sized effective action target")
        verify(collapse.width >= 48 && collapse.height >= 48,
               "the collapsed sidebar close target reaches 48dp")
        compare(actual._galleryColumnCountForTest, 3)
        const wallHost = wall.parent
        compare(wallHost.width, 318)
        const initialWallWidth = wall.width
        verify(initialWallWidth >= wallHost.width,
               "the real GridView includes its existing trailing gallery gap")

        touchItem(action)
        tryCompare(sidebar, "collapsed", false)
        compare(actual.contentLeft, 0)
        compare(wall.width, initialWallWidth,
                "opening Sources overlays the wall without narrowing it")
        compare(actual._galleryColumnCountForTest, 3)

        touchItem(collapse)
        tryCompare(sidebar, "collapsed", true)
        compare(actual.contentLeft, 0)
        compare(wall.width, initialWallWidth,
                "closing Sources keeps the wall width stable")
        compare(actual._galleryColumnCountForTest, 3)
    }

    function test_theatre_portrait_shelves_are_full_width_compact_and_overlay_rows() {
        const page = createTheatre(false)
        waitForRendering(page)
        verify(page.phonePortrait,
               "the actual TheatreCatalogPage sees the Android portrait Window")
        compare(page.contentLeft, 0)
        compare(page.x, 0)
        compare(page.width, 318)
        compare(page.mainRows.length, 2,
                "Theatre renders only the two local fixture rows")

        const top = assertPortraitRail(page, "top-10", true)
        const popular = assertPortraitRail(page, "popular", false)
        const rowsAction = findChild(page, "theatreRowsAction_movies")
        const rowIndex = findChild(theatreHost, "theatreRowIndexSidebar")
        verify(rowsAction && rowIndex,
               "the actual Rows action and RowIndexSidebar are present")
        verify(rowsAction.width >= 48 && rowsAction.height >= 48,
               "Rows exposes an effective 48dp target")
        const initialPageWidth = page.width
        const initialContentLeft = page.contentLeft
        touchItem(rowsAction)
        tryCompare(rowIndex, "collapsed", false)
        compare(page.contentLeft, initialContentLeft,
                "opening the row index overlays the page")
        compare(page.width, initialPageWidth,
                "opening the row index leaves catalogue content width unchanged")
        verify(top.rail.width === 318 && popular.rail.width === 318,
               "both actual rails keep the full portrait width")

        const rowIndexToggle = findChild(rowIndex, "theatreRowIndexCollapseAction")
        verify(rowIndexToggle !== null,
               "the expanded row index exposes its real collapse touch action")
        verify(rowIndexToggle.width >= 48 && rowIndexToggle.height >= 48,
               "the row index collapse target is at least 48dp")
        touchItem(rowIndexToggle)
        tryCompare(rowIndex, "collapsed", true)
        compare(page.contentLeft, initialContentLeft,
                "closing the row index keeps the catalogue at full width")
        compare(page.width, initialPageWidth,
                "closing the row index does not change page width")
    }

    function test_theatre_loading_skeleton_matches_compact_poster_rail() {
        const page = createTheatre(true)
        tryCompare(page, "loading", true)
        const skeletonCards = []
        const all = []
        allDescendants(page, all)
        for (let i = 0; i < all.length; ++i) {
            const item = all[i]
            if (item.visible && item.radius === 12 && item.width >= 84
                    && item.width <= 106 && item.height > 100
                    && Math.abs(item.height / item.width - 1.5) < 0.02)
                skeletonCards.push(item)
        }
        skeletonCards.sort(function(a, b) {
            return a.mapToItem(page, 0, 0).x - b.mapToItem(page, 0, 0).x
        })
        verify(skeletonCards.length >= 3,
               "the pending catalogue shows at least three compact local poster skeletons; found "
               + skeletonCards.length)
        for (let i = 0; i < 3; ++i) {
            compare(skeletonCards[i].width, 98)
            compare(skeletonCards[i].height, 147)
            const position = skeletonCards[i].mapToItem(page, 0, 0)
            verify(position.x >= -1 && position.x + skeletonCards[i].width <= page.width + 1,
                   "loading skeleton " + i + " stays inside the phone page")
        }

        verify(pendingCataloguePush !== null,
               "the local catalogue loader has a deferred response callback")
        pendingCataloguePush({ pageKey: "movies", generation: pendingCatalogueGeneration,
                               rows: makeCatalogueRows(), loading: false, error: "" })
        tryCompare(page, "loading", false)
        const topShelf = shelfForKey(page, "top-10")
        tryCompare(topShelf, "railLoaded", true)
        const rail = railInShelf(topShelf)
        verify(rail !== null)
        compare(rail.posterWidth, 98)
        compare(rail.posterHeight, 147)
        verify(Math.abs(topShelf.reservedHeight - rail.implicitHeight) <= 1,
               "the populated shelf reserves exactly the rendered compact rail height")
    }

    function test_landscape_and_desktop_keep_legacy_sizing() {
        destroyFixtures()
        setViewport(792, 354)
        const landscapeBrowser = createDiscover()
        verify(landscapeBrowser.phoneNarrow)
        compare(landscapeBrowser.compactPhonePortrait, false,
                "landscape does not select the portrait compact gallery")
        compare(landscapeBrowser.contentLeft, 64)
        const landscapeWall = findChild(landscapeBrowser, "fixtureDiscoverWall")
        verify(landscapeWall !== null)
        const landscapeCount = landscapeBrowser._galleryColumnCountForTest
        verify(landscapeCount >= 3)
        landscapeBrowser.destroy()
        browser = null

        const landscapePage = createTheatre(false)
        compare(landscapePage.phonePortrait, false)
        compare(landscapePage.contentLeft, 64)
        compare(landscapePage.width, 728)
        const landscapeTop = shelfForKey(landscapePage, "top-10")
        const landscapeRail = railInShelf(landscapeTop)
        verify(landscapeRail !== null)
        compare(landscapeRail.posterWidth, 148)
        compare(landscapeRail.cardGap, 30,
                "ranked Top 10 keeps its legacy landscape separation")
        const landscapePopularRail = railInShelf(shelfForKey(landscapePage, "popular"))
        verify(landscapePopularRail !== null)
        compare(landscapePopularRail.cardGap, 20,
                "ordinary landscape shelves keep their legacy gallery gap")

        destroyFixtures()
        setViewport(1280, 800)
        const desktopBrowser = createDiscover()
        compare(desktopBrowser.phoneNarrow, false)
        compare(desktopBrowser.compactPhonePortrait, false)
        compare(desktopBrowser.contentLeft, 80)
        compare(desktopBrowser.wallPosterWidth, 132)
        const desktopPage = createTheatre(false)
        compare(desktopPage.phonePortrait, false)
        compare(desktopPage.contentLeft, 80)
        compare(desktopPage.width, 1200)
        const desktopRail = railInShelf(shelfForKey(desktopPage, "top-10"))
        verify(desktopRail !== null)
        compare(desktopRail.posterWidth, 148)
        compare(desktopRail.cardGap, 30,
                "ranked desktop Top 10 keeps its legacy separation")
        const desktopPopularRail = railInShelf(shelfForKey(desktopPage, "popular"))
        verify(desktopPopularRail !== null)
        compare(desktopPopularRail.cardGap, 20,
                "ordinary desktop shelves keep their legacy gallery gap")
    }
}

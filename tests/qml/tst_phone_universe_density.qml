import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/Universes.js" as UDB

TestCase {
    id: tests
    name: "PhoneUniverseDensity"
    when: windowShown

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
        Item {
            id: pageHost
            width: host.width
            height: host.height
        }
    }

    Component {
        id: shelvesComponent
        App.ShelvesUniversePage { anchors.fill: parent }
    }
    Component {
        id: sagaComponent
        App.SagaUniversePage { anchors.fill: parent }
    }

    property var shelvesPage: null
    property var sagaPage: null

    function fixtureShelves() {
        const cfg = UDB.configFor("James Bond")
        const localWorks = cfg.shelves[0].runs[0].works.slice(0, 3)
        const works = localWorks.map(function(entry, index) {
            return {
                id: entry.id,
                key: "movie:" + index,
                canonIndex: index,
                title: entry.t,
                medium: "movie",
                type: "movie",
                year: entry.year,
                resolved: true,
                cover: ""
            }
        })
        return {
            name: "James Bond",
            category: cfg.category,
            blurb: cfg.blurb,
            blurbSource: "",
            banner: "",
            wallpaper: "",
            color: cfg.c1,
            catalogueNotice: "",
            shelves: [{
                key: "phone-density-fixture",
                title: cfg.shelves[0].title,
                tab: cfg.shelves[0].title,
                kind: "movie",
                runs: [{ label: cfg.shelves[0].runs[0].label, works: works }]
            }]
        }
    }

    function canonicalTitle(entry) {
        return entry && entry.t !== undefined ? String(entry.t) : String(entry || "")
    }

    function fixtureSaga(cfg) {
        const books = (cfg.novels || []).map(function(entry, index) {
            return {
                id: "phone-saga-book-" + index,
                canonIndex: index,
                title: canonicalTitle(entry),
                medium: "book",
                author: "George R. R. Martin",
                year: 1996 + index,
                resolved: true,
                cover: ""
            }
        })
        const shows = (cfg.shows || []).map(function(entry, index) {
            return {
                id: "phone-saga-show-" + index,
                canonIndex: index,
                title: canonicalTitle(entry),
                medium: "series",
                type: "series",
                year: 2011 + index,
                resolved: true,
                cover: "",
                art: ""
            }
        })
        return {
            name: "A Song of Ice and Fire",
            blurb: "Local saga fixture.",
            banner: "",
            metaline: "",
            books: books,
            films: [],
            shows: shows,
            comics: null
        }
    }

    function setAndroidHost(page) {
        if (page && page.hasOwnProperty("androidHost"))
            page.androidHost = host.androidHost
    }

    function setViewport(windowWidth, windowHeight, pageWidth, pageHeight) {
        host.width = windowWidth
        host.height = windowHeight
        pageHost.width = pageWidth === undefined ? windowWidth : pageWidth
        pageHost.height = pageHeight === undefined ? windowHeight : pageHeight
        setAndroidHost(shelvesPage)
        setAndroidHost(sagaPage)
        wait(40)
    }

    function init() {
        host.androidHost = true
        setViewport(354, 792)
        shelvesPage = shelvesComponent.createObject(pageHost)
        sagaPage = sagaComponent.createObject(pageHost)
        verify(shelvesPage !== null, "the production Shelves page creates in the owning Window")
        verify(sagaPage !== null, "the production Saga page creates in the owning Window")
        host.requestActivate()
        setAndroidHost(shelvesPage)
        setAndroidHost(sagaPage)
        shelvesPage.uni = fixtureShelves()
        sagaPage.uni = { name: "A Song of Ice and Fire", books: [], films: [], shows: [], comics: null }
        wait(10)
        sagaPage.uni = fixtureSaga(sagaPage.guide)
        wait(60)
    }

    function cleanup() {
        if (shelvesPage)
            shelvesPage.destroy()
        if (sagaPage)
            sagaPage.destroy()
        shelvesPage = null
        sagaPage = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const result = findChild(children[i], objectName)
            if (result)
                return result
        }
        return null
    }

    function findSagaTile(parent, id) {
        if (!parent)
            return null
        try {
            if (parent.item && parent.item.id === id && parent.form !== undefined)
                return parent
        } catch (error) { }
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const result = findSagaTile(children[i], id)
            if (result)
                return result
        }
        return null
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function test_shelves_portrait_fits_three_local_posters_and_metadata() {
        setViewport(354, 792)
        compare(host.width, 354)
        compare(host.height, 792)
        verify(shelvesPage.tileW >= 64 && shelvesPage.tileW <= 98,
               "three shelf posters use the readable phone width; current=" + shelvesPage.tileW)
        verify(shelvesPage.posterDensity,
               "poster density follows the portrait owning Window")
        compare(shelvesPage.tileGap, 12)
        compare(shelvesPage.tileW, 90,
                "the real shelf viewport fits three posters and two 12dp gaps")
        compare(shelvesPage.tileH, Math.round(shelvesPage.tileW * 1.5),
                "phone posters retain the 2:3 art ratio")

        const row = findChild(shelvesPage, "shelvesRow_phone-density-fixture")
        verify(row !== null, "the fixture uses the production shelf row")
        const third = findChild(shelvesPage, "shelvesCover_movie:2")
        verify(third !== null, "the third local title creates a renderable cover item")
        const tile = third.parent.parent
        compare(tile.width, shelvesPage.tileW)
        compare(tile.height, shelvesPage.tileH)
        const thirdRect = rectIn(tile, row)
        verify(thirdRect.x >= 0 && thirdRect.x + thirdRect.width <= row.width + 0.1,
               "the third complete shelf poster stays inside the real panel viewport")

        const title = findChild(shelvesPage, "shelvesCaption_movie:2")
        const metadata = findChild(shelvesPage, "shelvesMetadata_movie:2")
        verify(title !== null && metadata !== null,
               "the poster retains separate title and year captions")
        compare(title.text, "Goldfinger")
        verify(title.font.pixelSize >= 13 && title.maximumLineCount === 2,
               "the compact title is readable and has a reserved two-line caption")
        compare(metadata.text, "1964")
        verify(metadata.visible && metadata.font.pixelSize >= 11 && metadata.width >= 64,
               "year metadata remains readable below the title")
    }

    function test_saga_portrait_fits_three_grid_posters_and_preserves_relationships() {
        setViewport(354, 792)
        compare(sagaPage.uni.books.length, 9,
                "the fixture uses the supplied local Westeros canon list")
        compare(sagaPage.grid.cols, 5)
        compare(sagaPage.grid.stories.length, 5)
        verify(sagaPage.tileW >= 64 && sagaPage.tileW <= 98,
               "Saga posters stay within readable phone bounds; current=" + sagaPage.tileW)
        verify(sagaPage.posterDensity,
               "Saga poster density follows the portrait owning Window")
        compare(sagaPage.tileGap, 12)
        compare(sagaPage.tileW, 82,
                "the five-column local canon uses the width for three visible story columns")
        const visibleColumns = Math.min(3, Math.max(1, sagaPage.grid.cols))
        const expectedTileWidth = Math.max(64, Math.min(98,
            Math.floor((sagaPage.panelInner - (visibleColumns - 1) * 12) / visibleColumns)))
        compare(sagaPage.tileW, expectedTileWidth,
                "wide narrative grids size to three visible columns, not every story column")
        compare(sagaPage.tileH, Math.round(sagaPage.tileW * 1.5),
                "Saga phone posters retain the 2:3 art ratio")

        const row = sagaPage.grid.rows[0]
        compare(row.cells.length, 5)
        const gridScroll = findChild(sagaPage, "sagaGridScroll")
        verify(gridScroll !== null, "the production Saga grid scroll viewport is present")
        compare(sagaPage.grid.cols, 5,
                "the local canon keeps all five logical story columns")
        for (let i = 0; i < 3; ++i) {
            const cell = row.cells[i]
            verify(cell.form === "tile" && cell.span === 1,
                   "the canonical first three Westeros stories remain separate cells")
            const tile = findSagaTile(sagaPage, "phone-saga-book-" + i)
            verify(tile !== null, "book poster " + i + " is rendered")
            compare(tile.width, sagaPage.tileW)
            compare(tile.height, sagaPage.tileH)
            const rect = rectIn(tile, gridScroll)
            verify(rect.x >= 0 && rect.x + rect.width <= gridScroll.width + 0.1,
                   "book poster " + i + " is fully inside the Saga grid viewport")
        }
        const fourthTile = findSagaTile(sagaPage, "phone-saga-book-3")
        verify(fourthTile !== null, "the fourth story remains in the horizontally scrollable grid")
        const fourthRect = rectIn(fourthTile, gridScroll)
        verify(fourthRect.x >= gridScroll.width,
               "the panel fits three complete stories before the fourth column continues offscreen")

        const spanningCell = sagaPage.grid.rows[1].cells[0]
        compare(spanningCell.form, "banner")
        compare(spanningCell.span, 5,
                "the locally curated show continues to span all five book stories")
        const bannerTile = findSagaTile(sagaPage, "phone-saga-show-0")
        verify(bannerTile !== null)
        compare(bannerTile.width, sagaPage.tileW * spanningCell.span
                               + (spanningCell.span - 1) * sagaPage.tileGap)

        const focus = findChild(sagaPage, "sagaGridFocus")
        verify(focus !== null, "the relational grid keeps one keyboard traversal stop")
        focus.forceActiveFocus()
        wait(10)
        verify(focus.activeFocus, "the grid traversal item takes focus; host active=" + host.active)
        keyClick(Qt.Key_Right)
        compare(focus.cell, 1)
        compare(focus.current.item.id, "phone-saga-book-1",
                "horizontal traversal still follows the local story order")

        const branchTitle = findChild(sagaPage, "sagaShelfTitle_book:5")
        const branchMetadata = findChild(sagaPage, "sagaShelfMetadata_book:5")
        verify(branchTitle !== null && branchMetadata !== null,
               "the local branch shelf retains its title and author metadata")
        compare(branchTitle.text, "A Knight of the Seven Kingdoms")
        verify(branchTitle.font.pixelSize >= 13 && branchTitle.maximumLineCount === 2)
        compare(branchMetadata.text, "George R. R. Martin")
        verify(branchMetadata.visible && branchMetadata.font.pixelSize >= 11)
    }

    function test_landscape_tablet_and_desktop_keep_pre_density_poster_geometry() {
        setViewport(792, 354)
        verify(!shelvesPage.posterDensity,
               "a 792x354 landscape owning Window does not activate poster density")
        verify(!sagaPage.posterDensity,
               "a landscape owning Window does not activate Saga poster density")
        compare(shelvesPage.tileW, 164)
        compare(shelvesPage.tileH, 246)
        compare(shelvesPage.tileGap, 16)
        const sagaLandscapeWidth = Math.max(124, Math.min(sagaPage.tileMax,
            Math.floor((sagaPage.panelInner - (sagaPage.grid.cols - 1) * 16) / sagaPage.grid.cols)))
        compare(sagaPage.tileW, sagaLandscapeWidth,
                "792x354 retains the prior Saga grid measurement")
        compare(sagaPage.tileGap, 16)

        host.width = 792
        host.height = 354
        pageHost.width = 354
        pageHost.height = 792
        setAndroidHost(shelvesPage)
        setAndroidHost(sagaPage)
        wait(40)
        compare(shelvesPage.layout.usableWidth,354)
        compare(shelvesPage.layout.viewportHeight,354)
        verify(shelvesPage.posterDensity && sagaPage.posterDensity,
               "a narrow pane uses its clipped usable viewport instead of the raw Window width")
        compare(shelvesPage.tileW,90)
        compare(sagaPage.tileW,82)

        setViewport(800, 1280)
        verify(!shelvesPage.posterDensity && !sagaPage.posterDensity,
               "an 800x1280 Android tablet keeps the original poster density")
        compare(shelvesPage.tileW, 164)
        compare(shelvesPage.tileH, 246)
        compare(shelvesPage.tileGap, 16)
        const sagaTabletWidth = Math.max(124, Math.min(sagaPage.tileMax,
            Math.floor((sagaPage.panelInner - (sagaPage.grid.cols - 1) * 16) / sagaPage.grid.cols)))
        compare(sagaPage.tileW, sagaTabletWidth,
                "tablet Saga geometry keeps its existing full-column fit")
        compare(sagaPage.tileGap, 16)

        setViewport(1280, 720)
        host.androidHost = false
        setAndroidHost(shelvesPage)
        setAndroidHost(sagaPage)
        wait(40)
        verify(!shelvesPage.posterDensity && !sagaPage.posterDensity)
        compare(shelvesPage.tileW, 164)
        compare(shelvesPage.tileH, 246)
        compare(shelvesPage.tileGap, 16)
        const sagaDesktopWidth = Math.max(124, Math.min(sagaPage.tileMax,
            Math.floor((sagaPage.panelInner - (sagaPage.grid.cols - 1) * 16) / sagaPage.grid.cols)))
        compare(sagaPage.tileW, sagaDesktopWidth)
        compare(sagaPage.tileGap, 16)
    }
}

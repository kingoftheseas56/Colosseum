import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneBrowseBounds"
    when: windowShown

    property var mosaic: null
    readonly property var genreFixtures: [
        { name: "An exceptionally long genre name that must wrap inside its card", count: 24 },
        { name: "Adventure", count: 18 },
        { name: "Drama", count: 12 },
        { name: "Fantasy", count: 9 },
        { name: "Mystery", count: 7 }
    ]
    readonly property var presentationItems: [
        { id: "fixture-1", type: "movie", title: "Bounded presentation card", cover: "", year: 2026 }
    ]

    Window {
        id: host
        width: 354
        height: 844
        visible: true
        property bool androidHost: true

        Loader {
            id: tabLoader
            x: 18
            y: 0
            width: 318
            height: item ? item.implicitHeight : 0
        }
    }

    Component { id: mangaTabComponent; App.TankobanMangaTab { } }
    Component { id: comicsTabComponent; App.TankobanComicsTab { } }
    Component { id: mosaicComponent; App.GenreMosaic { } }
    Component { id: browserComponent; App.DiscoverBrowser { } }
    Component { id: widgetHeaderComponent; App.WidgetHeader { } }
    Component { id: lazyShelfComponent; App.LazyPosterShelf { } }
    Component { id: posterRailComponent; App.PosterRail { } }

    function init() {
        host.androidHost = true
        host.width = 354
        host.height = 844
        tabLoader.width = 318
        tabLoader.sourceComponent = null
    }

    function cleanup() {
        tabLoader.sourceComponent = null
        if (mosaic) {
            mosaic.destroy()
            mosaic = null
        }
    }

    function assertTabBounds(source, contentName, loaderWidth, windowWidth, windowHeight) {
        host.width = windowWidth
        host.height = windowHeight
        tabLoader.width = loaderWidth
        tabLoader.sourceComponent = source
        tryVerify(function() { return tabLoader.item !== null }, 3000)
        wait(0)

        const tab = tabLoader.item
        compare(tab.width, loaderWidth)
        const content = findChild(tab, contentName)
        const rail = findChild(tab, "tankobanRowIndexSidebar")
        verify(content !== null, contentName + " is present in the real loaded tab")
        verify(rail !== null, "the row index rail is inside the real loaded tab")
        compare(rail.width, 52)
        compare(content.x, 64)
        compare(content.width, loaderWidth - 64)

        const contentRight = content.mapToItem(null, content.width, 0).x
        const loaderRight = tabLoader.mapToItem(null, tabLoader.width, 0).x
        verify(contentRight <= loaderRight + 1,
               contentName + " right edge " + contentRight + " stays within Loader right edge " + loaderRight)
        return { tab: tab, content: content, rail: rail }
    }

    function textNodeWithValue(node, value) {
        if (!node)
            return null
        if (node.text === value)
            return node
        const children = node.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = textNodeWithValue(children[i], value)
            if (found)
                return found
        }
        return null
    }

    function test_actual_loader_tab_content_stays_inside_portrait_and_landscape() {
        assertTabBounds(mangaTabComponent, "mangaTabContent", 318, 354, 844)
        assertTabBounds(comicsTabComponent, "comicsTabContent", 318, 354, 844)
        assertTabBounds(mangaTabComponent, "mangaTabContent", 756, 844, 390)
        assertTabBounds(comicsTabComponent, "comicsTabContent", 756, 844, 390)
    }

    function test_phone_widget_header_bounds_and_touch_delivery() {
        host.width = 354
        host.height = 844
        host.androidHost = true
        const titleText = "Explore by Genre — Manga"
        const header = createTemporaryObject(widgetHeaderComponent, host.contentItem,
            { x: 18, y: 18, width: 254, title: titleText,
              moreLabel: "Explore", navigable: true,
              automationId: "phoneGenreExploreAction" })
        verify(header !== null)
        wait(0)
        verify(header.phoneLayout)
        compare(header.implicitHeight, 48)
        header.height = header.implicitHeight
        compare(header.height, 48)

        const title = textNodeWithValue(header, titleText)
        const action = findChild(header, "phoneGenreExploreAction")
        verify(title !== null, "the visible title text is present")
        verify(title.visible)
        verify(action !== null, "the right action exposes its actual tap target")
        verify(action.visible && action.enabled && action.pointerEnabled)

        const titlePosition = title.mapToItem(header, 0, 0)
        const titleLeft = titlePosition.x
        const titleTop = titlePosition.y
        const titleRight = titleLeft + title.paintedWidth
        const titleBottom = titleTop + title.paintedHeight
        const actionPosition = action.mapToItem(header, 0, 0)
        const actionLeft = actionPosition.x
        const actionTop = actionPosition.y
        const actionRight = actionLeft + action.width
        const actionBottom = actionTop + action.height
        const overlaps = titleLeft < actionRight && actionLeft < titleRight
            && titleTop < actionBottom && actionTop < titleBottom
        const issues = []
        if (overlaps)
            issues.push("painted title [" + titleLeft + "," + titleRight + "] overlaps action ["
                        + actionLeft + "," + actionRight + "]")
        if (!(titleLeft >= 0 && titleTop >= 0
              && titleRight <= header.width && titleBottom <= header.height))
            issues.push("painted title escapes the 254x" + header.height + " header")
        if (!(action.width >= 48 && action.height >= 48))
            issues.push("effective target is " + action.width + "x" + action.height + "dp, below 48dp")

        let tapCount = 0
        header.moreClicked.connect(function() { ++tapCount })
        mouseClick(action, action.width / 2, action.height / 2)
        wait(0)
        if (tapCount !== 1)
            issues.push("center tap emitted moreClicked " + tapCount + " times; expected once")

        const topPoint = action.mapToItem(host.contentItem, action.width / 2, 1)
        const topTouch = touchEvent(host.contentItem)
        topTouch.press(0, host.contentItem, topPoint.x, topPoint.y).commit()
        wait(20)
        touchEvent(host.contentItem).release(0, host.contentItem, topPoint.x, topPoint.y).commit()
        wait(20)
        if (tapCount !== 2)
            issues.push("near-top touch outside the header paint box left moreClicked at " + tapCount)

        const bottomPoint = action.mapToItem(host.contentItem, action.width / 2, action.height - 1)
        const bottomTouch = touchEvent(host.contentItem)
        bottomTouch.press(0, host.contentItem, bottomPoint.x, bottomPoint.y).commit()
        wait(20)
        touchEvent(host.contentItem).release(0, host.contentItem, bottomPoint.x, bottomPoint.y).commit()
        wait(20)
        if (tapCount !== 3)
            issues.push("near-bottom touch outside the header paint box left moreClicked at " + tapCount)

        verify(issues.length === 0,
               issues.join("; ") + " (titlePaintedWidth=" + title.paintedWidth
               + ", actionTarget=" + action.width + "x" + action.height + ")")
    }

    function test_phone_sidebar_expands_over_content_without_resizing_it() {
        const loaded = assertTabBounds(mangaTabComponent, "mangaTabContent", 318, 354, 844)
        const initialContentWidth = loaded.content.width
        const toggle = findChild(loaded.tab, "tankobanRowIndexCollapseAction")
        verify(toggle !== null, "the actual rail exposes its collapse action")

        mouseClick(toggle)
        tryCompare(loaded.rail, "width", 240, 1500)
        compare(loaded.content.x, 64)
        compare(loaded.content.width, initialContentWidth)

        mouseClick(toggle)
        tryCompare(loaded.rail, "width", 52, 1500)
        compare(loaded.content.x, 64)
        compare(loaded.content.width, initialContentWidth)

        const comics = assertTabBounds(comicsTabComponent, "comicsTabContent", 318, 354, 844)
        const comicsWidth = comics.content.width
        const comicsToggle = findChild(comics.tab, "tankobanRowIndexCollapseAction")
        verify(comicsToggle !== null)
        mouseClick(comicsToggle)
        tryCompare(comics.rail, "width", 240, 1500)
        compare(comics.content.x, 64)
        compare(comics.content.width, comicsWidth)
        mouseClick(comicsToggle)
        tryCompare(comics.rail, "width", 52, 1500)
        compare(comics.content.width, comicsWidth)

        const landscape = assertTabBounds(mangaTabComponent, "mangaTabContent", 756, 844, 390)
        const landscapeWidth = landscape.content.width
        mouseClick(findChild(landscape.tab, "tankobanRowIndexCollapseAction"))
        tryCompare(landscape.rail, "width", 240, 1500)
        compare(landscape.content.width, landscapeWidth)
    }

    function test_genre_mosaic_uses_phone_columns_and_bounded_wrapping_labels() {
        host.width = 354
        host.height = 844
        host.androidHost = true
        mosaic = createTemporaryObject(mosaicComponent, host.contentItem, { width: 318 })
        verify(mosaic !== null)
        mosaic.genres = genreFixtures
        tryCompare(mosaic, "columns", 2)
        verify(mosaic.phoneLayout)

        const longLabel = textNodeWithValue(mosaic, genreFixtures[0].name)
        verify(longLabel !== null, "the long genre label is rendered")
        compare(longLabel.wrapMode, Text.WordWrap)
        compare(longLabel.maximumLineCount, 2)
        const cardWidth = longLabel.parent.width
        verify(longLabel.width <= cardWidth - 26 + 1,
               "phone label width " + longLabel.width + " fits card width " + cardWidth)
        verify(longLabel.lineCount >= 2, "long phone labels wrap onto a second line")
        verify(longLabel.lineCount <= 2, "phone labels remain capped at two lines")
        verify(longLabel.height <= longLabel.parent.height,
               "the label remains inside its card height")
    }

    function test_tablet_and_narrow_desktop_keep_non_phone_genre_layout() {
        host.width = 1280
        host.height = 800
        host.androidHost = true
        mosaic = createTemporaryObject(mosaicComponent, host.contentItem, { width: 1000 })
        verify(mosaic !== null)
        compare(mosaic.phoneLayout, false)
        compare(mosaic.columns, 5)

        mosaic.destroy()
        mosaic = null
        tabLoader.sourceComponent = null
        host.width = 354
        host.height = 844
        host.androidHost = false
        mosaic = createTemporaryObject(mosaicComponent, host.contentItem, { width: 318 })
        verify(mosaic !== null)
        compare(mosaic.phoneLayout, false)
        compare(mosaic.columns, 5)
    }

    function test_discover_gallery_uses_two_phone_columns_without_an_adapter() {
        host.width = 354
        host.height = 844
        host.androidHost = true
        const browser = createTemporaryObject(browserComponent, host.contentItem,
            { x: 18, y: 0, width: 318, height: 780,
              adapter: null, posterVisualProfile: "gallery", items: presentationItems })
        verify(browser !== null)
        compare(browser.adapter, null)
        compare(browser.initialized, true)
        compare(browser.phoneNarrow, true)
        compare(browser.automationItemCount, 1)
        tryCompare(browser, "_galleryColumnCountForTest", 2)
        verify(browser._galleryDelegateWidthForTest >= 112,
               "gallery delegate remains readable at " + browser._galleryDelegateWidthForTest + "px")
        verify(browser._galleryDelegateWidthForTest < browser.width,
               "gallery delegate remains bounded by the phone viewport")
        tabLoader.sourceComponent = null
        host.androidHost = false
        tryCompare(browser, "phoneNarrow", false)
        tryCompare(browser, "_galleryColumnCountForTest", 3)
        browser.destroy()
    }

    function test_phone_lazy_shelf_reservation_matches_poster_rail_height() {
        const row = { title: "Bounded shelf", items: presentationItems,
                      ranked: false, sourceKind: "house", sourceLabel: "", seeAllPin: null }
        const sizes = [
            { windowWidth: 354, windowHeight: 844, contentWidth: 318 },
            { windowWidth: 844, windowHeight: 390, contentWidth: 756 }
        ]
        host.androidHost = true

        for (let i = 0; i < sizes.length; ++i) {
            host.width = sizes[i].windowWidth
            host.height = sizes[i].windowHeight
            const shelf = createTemporaryObject(lazyShelfComponent, host.contentItem,
                { width: sizes[i].contentWidth, row: row, visualProfile: "gallery" })
            const rail = createTemporaryObject(posterRailComponent, host.contentItem,
                { width: sizes[i].contentWidth, title: row.title, items: row.items,
                  ranked: row.ranked, sourceKind: row.sourceKind,
                  sourceLabel: row.sourceLabel, seeAllPin: row.seeAllPin,
                  visualProfile: "gallery" })
            verify(shelf !== null && rail !== null)
            tryVerify(function() { return rail.implicitHeight > 0 }, 3000)
            compare(shelf.headerHeight, 48)
            verify(Math.abs(shelf.reservedHeight - rail.implicitHeight) <= 1,
                   "reserved shelf height " + shelf.reservedHeight
                   + " matches actual PosterRail implicit height " + rail.implicitHeight
                   + " at " + sizes[i].windowWidth + "x" + sizes[i].windowHeight)
            shelf.destroy()
            rail.destroy()
            wait(0)
        }
    }
}

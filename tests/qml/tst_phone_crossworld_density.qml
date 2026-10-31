import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneCrossworldDensity"
    when: windowShown

    Window {
        id: host
        width: 354; height: 792; visible: true
        property bool androidHost: true
        Item { id: board; x: 18; width: host.width - 36; height: host.height }
    }
    Component { id: tankDiscover; App.TankobanDiscoverPage { anchors.fill: parent; active: false } }
    Component { id: bookDiscover; App.BiblioDiscoverPage { anchors.fill: parent; active: false } }
    Component {
        id: mangaFixture
        Flickable {
            id: mangaFlick
            anchors.fill: parent
            contentWidth: width; contentHeight: page.implicitHeight
            property alias page: manga
            App.TankobanMangaTab { id: manga; pageFlick: mangaFlick; height: implicitHeight }
        }
    }
    Component {
        id: comicsFixture
        Flickable {
            id: comicsFlick
            anchors.fill: parent
            contentWidth: width; contentHeight: page.implicitHeight
            property alias page: comics
            property bool deferShelf: true
            App.TankobanComicsTab {
                id: comics
                pageFlick: comicsFlick; height: implicitHeight
                viewportHeight: comicsFlick.height
                viewportTop: comicsFlick.deferShelf ? 100000 : 0
                comicRows: testCase.localCards()
                comicShelves: [{label: "Local comics", rows: testCase.localCards()}]
            }
        }
    }
    Component {
        id: exploreFixture
        App.BiblioExplorePage {
            anchors.fill: parent
            active: false; catalogSource: null; extensionsSource: null
        }
    }
    SignalSpy { id: rowsActionSpy; signalName: "triggered" }
    function init() { host.androidHost = true; host.width = 354; host.height = 792; wait(30) }
    function localCards() {
        const cards = []
        for (let i = 0; i < 6; ++i) cards.push({caption: "Local comic " + i, cover: ""})
        return cards
    }
    function find(root, name) {
        if (root.objectName === name) return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const match = find(children[i], name)
            if (match) return match
        }
        return null
    }
    function touch(item) {
        verify(item !== null)
        const point = item.mapToItem(host.contentItem, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.x <= host.width && point.y >= 0 && point.y <= host.height)
        touchEvent(host.contentItem).press(0, host.contentItem, point.x, point.y).commit()
        wait(20)
        touchEvent(host.contentItem).release(0, host.contentItem, point.x, point.y).commit()
        wait(280)
    }
    function test_discover_data() { return [{tag: "tankoban", comp: tankDiscover}, {tag: "biblio", comp: bookDiscover}] }
    function test_discover(data) {
        const page = createTemporaryObject(data.comp, board)
        verify(page !== null)
        const browser = find(page, data.tag + "DiscoverBrowser")
        verify(browser !== null)
        compare(page.contentLeft, 0)
        compare(browser._galleryColumnCountForTest, 3)
        compare(browser._galleryDelegateWidthForTest, 98)
        const sidebar = find(browser, data.tag + "DiscoverSidebar")
        const action = find(browser, data.tag + "DiscoverSourcesAction")
        verify(action.height >= 48)
        touch(action)
        compare(sidebar.collapsed, false)
        touch(find(sidebar, data.tag + "DiscoverSidebarCollapse"))
        compare(sidebar.collapsed, true)
        compare(page.contentLeft, 0)
        host.width = 792; host.height = 354; wait(40)
        compare(browser.compactPhonePortrait, false)
        compare(page.contentLeft, 64)
    }
    function test_rows_data() {
        return [
            {tag: "manga", comp: mangaFixture, action: "tankobanMangaRowsAction", prefix: "tankobanRowIndex"},
            {tag: "comics", comp: comicsFixture, action: "tankobanComicsRowsAction", prefix: "tankobanRowIndex"},
            {tag: "books", comp: exploreFixture, action: "biblioExploreRowsAction", prefix: "biblioRowIndex"}
        ]
    }
    function test_rows(data) {
        const fixture = createTemporaryObject(data.comp, board)
        verify(fixture !== null)
        const page = data.tag === "books" ? fixture : fixture.page
        compare(page.contentLeft, 0)
        const sidebar = find(page, data.prefix + "Sidebar")
        verify(sidebar !== null)
        compare(sidebar.visible, false)
        const action = find(page, data.action)
        verify(action !== null && action.height >= 48)
        rowsActionSpy.target = action
        rowsActionSpy.clear()
        wait(80)
        touch(action)
        compare(rowsActionSpy.count, 1, "Rows action receives one ordinary touch")
        compare(sidebar.collapsed, false)
        compare(sidebar.visible, true)
        touch(find(sidebar, data.prefix + "CollapseAction"))
        compare(sidebar.collapsed, true)
        compare(sidebar.visible, false)
        compare(page.contentLeft, 0)
        host.width = 792; host.height = 354; wait(40)
        compare(page.phonePortrait, false)
        compare(page.contentLeft, 64)
        compare(sidebar.visible, true)
    }
    function test_comics_lazy_reservation_matches_compact_rail() {
        const fixture = createTemporaryObject(comicsFixture, board)
        verify(fixture !== null)
        wait(80)
        const slot = find(fixture.page, "tankobanComicsShelf_0")
        const loader = find(fixture.page, "tankobanComicsShelfLoader_0")
        verify(slot !== null && loader !== null)
        compare(slot.built, false)
        const reserve = slot.height
        fixture.deferShelf = false
        tryVerify(function() { return loader.status === Loader.Ready && loader.item !== null })
        compare(loader.item.effectivePosterWidth, 98)
        verify(loader.item.effectiveStripHeight >= 187)
        compare(slot.height, loader.item.effectiveRailHeight)
        compare(slot.height, reserve)
    }
}

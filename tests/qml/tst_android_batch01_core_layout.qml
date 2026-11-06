import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: suite
    name: "AndroidBatch01CoreLayout"
    when: windowShown
    Window {
        id: host
        width: 1280; height: 760; visible: true
        property bool androidHost: false
        property bool desktopPresentation: true
    }
    Item { id: testBackdrop; parent: host.contentItem; width: 1280; height: 760 }
    Component { id: bookComponent; App.BiblioBook { width: 1280; height: 720; extensionsRef: null } }
    Component { id: topComponent; App.TopBar { width: 1280; backdrop: testBackdrop; activeMedium: "Biblio"; lifecycleActive: false } }
    Component { id: houseComponent; App.HouseWindowControls {} }
    Component { id: bookSearchComponent; App.BiblioSearch { width: 1280; height: 720; historyStore: null } }
    Component { id: worldSearchComponent; App.SearchSurface { width: 1280; height: 720; historyStore: null } }
    Component { id: browserComponent; App.DiscoverBrowser { width: 1200; height: 700; active: false; automationPrefix: "layout" } }
    QtObject {
        id: adapter
        property string title: "Popular"
        property string provider: "Apple Books"
        function types() { return [{ key: "book", label: "Books" }, {key:"manga",label:"Manga"}, {key:"movie",label:"Movies"}] }
        function catalogs(type) { return [{key:"popular",title:title,sourceName:provider}, {key:"new",title:"New",sourceName:provider}] }
        function filters(type, catalog) { return [] }
    }
    function make(component, properties) {
        const item = createTemporaryObject(component, host.contentItem, properties || {})
        verify(item !== null)
        return item
    }
    function child(item, name) {
        const node = findChild(item, name)
        verify(node !== null, name)
        return node
    }
    function actions(item) {
        var result = []
        function walk(node) {
            if (typeof node.activate === "function" && typeof node.accessibleName === "string"
                    && /Minimize|fullscreen|Quit Colosseum|Close Colosseum|Toggle fullscreen|^Close$/.test(node.accessibleName)) result.push(node)
            const children = node.children || []
            for (var i=0; i<children.length; ++i) walk(children[i])
        }
        walk(item)
        return result
    }
    function noActiveWindowActions(item) {
        const list = actions(item)
        verify(list.length > 0)
        for (var i=0; i<list.length; ++i) {
            compare(list[i].visible, false)
            compare(list[i].focusEnabled, false)
            compare(list[i].activate(Qt.TabFocusReason), false)
        }
    }
    function rightIn(item, ancestor) { return item.mapToItem(ancestor, item.width, 0).x }
    function init() { adapter.title = "Popular"; adapter.provider = "Apple Books"; host.androidHost = false; host.width = 1280 }
    function test_biblio_outer_insets_and_safe_area() {
        const item = make(bookComponent, { androidHost: false, safeAreaLeft: 0, safeAreaRight: 0 })
        const header = child(item, "biblioBookHeader")
        const body = child(item, "biblioBookBody")
        compare(header.x, 54); compare(body.x, 54)
        compare(header.width, 1172); compare(body.width, 1172)
        item.androidHost = true
        compare(header.x, 54); compare(body.x, 54)
        compare(header.width, 1172); compare(body.width, 1172)
        item.safeAreaLeft = 70; item.safeAreaRight = 11
        compare(header.x, 70); compare(body.x, 70)
        compare(header.width, 1156); compare(body.width, 1156)
        const verbs = child(item, "biblioBookWindowControls")
        compare(verbs.width, 0)
        noActiveWindowActions(verbs)
        item.androidHost = false
        item.safeAreaLeft = 0; item.safeAreaRight = 0
        compare(header.x, 54); compare(body.x, 54)
        compare(verbs.visible, true)
    }
    function test_topbar_removes_only_window_verbs_and_reflows() {
        const item = make(topComponent, { androidHost: false })
        const chrome = child(item, "topBarSystemChrome")
        tryVerify(function() { return chrome.width > 0 })
        const desktopWidth = chrome.width
        compare(actions(item).length, 3)
        item.androidHost = true
        tryCompare(chrome, "width", desktopWidth - 3 * 22 - 3 * chrome.spacing)
        noActiveWindowActions(item)
        compare(child(item, "topBarSearch_Biblio").visible, true)
        compare(child(item, "topBarWallpaperButton").visible, true)
        item.androidHost = false
        tryCompare(chrome, "width", desktopWidth)
    }
    function test_house_collapses_occupied_size() {
        const item = make(houseComponent, { androidHost: false })
        compare(item.width, 106); compare(item.height, 22)
        item.visible = false
        compare(item.width, 0); compare(item.height, 0)
        item.visible = Qt.binding(function() { return !item.androidHost })
        item.androidHost = true
        compare(item.width, 0); compare(item.height, 0)
        noActiveWindowActions(item)
        item.androidHost = false
        wait(30)
        compare(item.width, 106); compare(item.height, 22)
    }
    function test_search_reservation_data() {
        return [{tag:"Biblio",component:bookSearchComponent,field:"biblioSearchField",controls:"biblioSearchWindowControls"},
                {tag:"World",component:worldSearchComponent,field:"worldSearchField",controls:"worldSearchWindowControls"}]
    }
    function test_search_reservation(data) {
        const item = make(data.component, { androidHost: false })
        const field = child(item, data.field)
        const chrome = child(item, data.controls)
        const desktopFieldWidth = field.width
        const desktopChromeWidth = chrome.width
        verify(desktopChromeWidth > 0)
        item.androidHost = true
        wait(30)
        compare(chrome.width, 0)
        compare(field.width, desktopFieldWidth + desktopChromeWidth + 24)
        noActiveWindowActions(chrome)
        item.width = 700
        compare(field.width, 668)
        item.width = 1280; item.androidHost = false
        wait(30)
        compare(field.width, desktopFieldWidth)
    }
    function test_catalogue_heading_keeps_desktop_alignment_data() {
        return [{tag:"wide-closed",viewport:1200,collapsed:true,longTitle:false,provider:"Apple Books"},
                {tag:"wide-open",viewport:1200,collapsed:false,longTitle:false,provider:"Cinemeta"},
                {tag:"stacked-long",viewport:600,collapsed:false,longTitle:true,provider:"Jikan"}]
    }
    function test_catalogue_heading_keeps_desktop_alignment(data) {
        adapter.provider = data.provider
        if (data.longTitle) adapter.title = "A very long selected catalogue title that must be elided"
        const item = make(browserComponent, { width:data.viewport, adapter:adapter, currentType:"book", currentCatalogKey:"popular" })
        const sidebar = child(item, "layoutDiscoverSidebar")
        sidebar.collapsed = data.collapsed
        item.items = [{id:"fixture",title:"Offline title",type:"book",cover:"",author:"Author"}]
        tryCompare(sidebar, "width", data.collapsed ? sidebar.closedWidth : sidebar.openWidth)
        tryVerify(function() { return findChild(item, "discoverCard_fixture") !== null })
        const wall = child(item, "layoutDiscoverWall")
        const poster = child(item, "discoverCard_fixture")
        const posterWidth = poster.width; const posterHeight = poster.height; const wallWidth = wall.width
        const beforePoster = poster.mapToItem(item, 0, 0)
        const beforeWall = wall.mapToItem(item, 0, 0)
        const fields = ["layoutDiscoverHeadingGroup","layoutDiscoverHeadingKicker","layoutDiscoverSummary","layoutDiscoverHeadingTitle","layoutDiscoverHeadingProvider"]
        const before = fields.map(function(name) { return rightIn(child(item,name),item) })
        item.androidHost = true
        wait(30)
        for (var i=0; i<fields.length; ++i) fuzzyCompare(rightIn(child(item,fields[i]),item), before[i], 0.01, fields[i])
        compare(child(item,"layoutDiscoverHeadingProvider").text, data.provider)
        compare(poster.width, posterWidth); compare(poster.height, posterHeight); compare(wall.width, wallWidth)
        fuzzyCompare(wall.mapToItem(item,0,0).x,beforeWall.x,0.01)
        fuzzyCompare(wall.mapToItem(item,0,0).y,beforeWall.y,0.01)
        fuzzyCompare(poster.mapToItem(item,0,0).x,beforePoster.x,0.01)
        fuzzyCompare(poster.mapToItem(item,0,0).y,beforePoster.y,0.01)
        fuzzyCompare(rightIn(child(item,"discoverCatalogMenu"),item),rightIn(child(item,"layoutDiscoverHeadingGroup"),item),0.01)
        if (data.longTitle) {
            compare(item.stackedTypeHeader, true)
            const title = child(item,"layoutDiscoverHeadingTitle")
            verify(title.width < title.implicitWidth)
            verify(title.mapToItem(item,0,0).x >= item.contentLeft)
        }
    }
}

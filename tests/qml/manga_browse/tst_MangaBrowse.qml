import QtQuick
import QtTest
import "../../../qml" as App

Rectangle {
    id: fixture
    color: "#101116"
    width: 1280; height: 720
    QtObject {
        id: catalog
        property bool remoteEnabled: false
        property int rowCount: 24
        signal readyChanged()
        function ready() { return true }
        function discoverPageFiltered(cat, filters, explicit, offset, count) {
            var rows = []
            for (var i = 0; i < Math.min(count, rowCount); ++i)
                rows.push({mal_id:i+1, title:"Manga " + (i+1), explicit:i===0, cover:""})
            return {items:rows, nextOffset:count, exhausted:false}
        }
        function discoverPageFilteredState() { return {source:"local", status:"success"} }
    }
    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: page.height
        App.TankobanMangaTab { id: page; malCatalog: catalog; pageFlick: flick }
    }
    SignalSpy { id: series; target: page; signalName: "seriesByIdRequested" }
    SignalSpy { id: discover; target: page; signalName: "discoverPinRequested" }
    TestCase {
        name: "MangaBrowse"
        when: windowShown
        function init() { catalog.rowCount=24; page.loadShelves(true); page.showExplicitContent=false; flick.contentY=0; series.clear(); discover.clear() }
        function test_renderFixture() {
            tryCompare(page,"shelvesLoading",false)
            wait(60)
            grabImage(fixture).save("manga-browse-top-fixture.png")
            var shelf=findChild(page,"tankobanMangaShelf_completed")
            flick.contentY=shelf.y
            tryCompare(shelf,"railLoaded",true)
            wait(60)
            grabImage(fixture).save("manga-browse-shelves-fixture.png")
        }
        function test_rowsAndCanonicalIdentity() {
            tryCompare(page,"shelvesLoading",false)
            compare(page.shelfRows.length,8)
            compare(page.popularShelf.items[0].id,"2")
            var shelf=findChild(page,"tankobanMangaShelf_completed")
            verify(shelf !== null)
            shelf.itemRequested(shelf.row.items[0])
            compare(series.count,1)
            compare(series.signalArguments[0][1],shelf.row.items[0].malId)
            shelf.seeAllRequested(shelf.row.seeAllPin)
            compare(discover.signalArguments[0][0].catalogId,"completed")
        }
        function test_explicitPartitionReloads() {
            page.showExplicitContent=true
            tryCompare(page,"shelvesLoading",false)
            compare(page.popularShelf.items[0].id,"1")
            page.showExplicitContent=false
            compare(page.popularShelf.items[0].id,"2")
        }
        function test_topSelectionClampedOnRefresh() {
            var strip=findChild(page,"tankobanTopMangaTile_keyboardStrip")
            verify(strip !== null)
            strip.currentIndex=9
            catalog.rowCount=5
            page.loadShelves(true)
            compare(page.popularShelf.items.length,4)
            verify(strip.currentIndex<4)
            strip.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(series.count,1)
        }
        function test_lazyRowsAndSidebar() {
            tryCompare(page,"shelvesLoading",false)
            var last=findChild(page,"tankobanMangaShelf_started-2010s")
            verify(last !== null)
            tryCompare(last,"railLoaded",false)
            flick.contentY=Math.max(0,last.y-100)
            tryCompare(last,"railLoaded",true)
            compare(page.rowIndexRows.length,10)
            verify(page.height>2000)
        }
    }
}

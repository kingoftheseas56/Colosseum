// Cold Discover successful asynchronous page delivery: retain skeleton cells
// while inserting real rows, and only then clear loading. The reverse ordering
// causes 24 extra visible delegate recreations on the app's first successful page.
// This focused test is intentionally RED on pinned 266671d and GREEN on the fix.
import QtQuick
import "../qml" as UI

Item {
    id: root
    width: 1280
    height: 800
    property var completePage: null
    property bool sawPopulatedWhileLoading: false
    property bool sawReady: false
    property int calls: 0
    property var measuredWall: null
    property bool duringDelivery: false
    property int minimumCellCount: 999999
    property int maximumCellCount: 0
    property int initialCellCount: 0
    function locateWall(item) {
        if (typeof item.syncCells === "function" && item.cellTarget !== undefined) return item
        var children = item.children || []
        for (var i=0; i<children.length; i++) {
            var found = locateWall(children[i])
            if (found) return found
        }
        return null
    }
    QtObject {
        id: asyncAdapter
        function types() { return [{key:"manga",label:"Manga"}] }
        function catalogs(type) {
            return [{key:"popular", title:"Popular",sourceKind:"builtin",
                     section:"Tankoban",attribution:"Tankoban built-in catalogue"}]
        }
        function filters(type, catalog) { return [] }
        function defaultCatalog(type) { return "popular" }
        function resolvePin(pin) {
            return {missing:false,type:"manga",catalogKey:"popular",filterGroup:"",filterKey:""}
        }
        function fetchPage(state, cursor, generation, done) {
            root.calls++
            root.completePage = function() {
                var rows = []
                for (var i = 0; i < 24; i++)
                    rows.push({id:String(i+1),type:"manga",title:"Manga "+(i+1),
                               cover:"",year:0,rating:0,format:"",
                               publisher:"",availability:true,explicit:false,raw:{}})
                done(generation,{items:rows,nextCursor:null,exhausted:true,
                                 freshness:"bundled",warning:""})
            }
        }
    }
    UI.DiscoverBrowser {
        id: browser
        anchors.fill: parent
        adapter: asyncAdapter
        fallbackType: "manga"
        onItemsChanged: {
            if (items.length === 24) {
                root.sawReady = true
                root.sawPopulatedWhileLoading = loading
            }
        }
    }
    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: {
            if (!root.completePage || root.calls !== 1) { Qt.exit(11); return }
            root.measuredWall = root.locateWall(browser)
            if (!root.measuredWall || root.measuredWall.model.count < 1) { Qt.exit(13); return }
            root.measuredWall.model.countChanged.connect(function() {
                if (root.duringDelivery) {
                    root.minimumCellCount = Math.min(root.minimumCellCount, root.measuredWall.model.count)
                    root.maximumCellCount = Math.max(root.maximumCellCount, root.measuredWall.model.count)
                }
            })
            root.minimumCellCount = root.measuredWall.model.count
            root.maximumCellCount = root.measuredWall.model.count
            root.initialCellCount = root.measuredWall.model.count
            root.duringDelivery = true
            root.completePage()
            Qt.callLater(function() {
                // Prove real nonempty delivery, settled state and stable cursor.
                // Captured onItemsChanged is the critical mid-transition assertion.
                var pass = root.sawReady && root.sawPopulatedWhileLoading
                           && browser.items.length === 24
                           && !browser.loading && browser.exhausted
                           && browser.cursor === null && root.calls === 1
                           && !browser.pageFailed
                           && root.minimumCellCount > 0 && root.measuredWall.model.count === 24
                           && root.maximumCellCount <= Math.max(root.initialCellCount, 24)
                root.duringDelivery = false
                console.log("ROOT_REAL_CELL_MIN " + root.minimumCellCount + " MAX " + root.maximumCellCount)
                console.log(pass ? "DISCOVER_COLD_ORDER_GREEN" : "DISCOVER_COLD_ORDER_RED")
                Qt.exit(pass ? 0 : 1)
            })
        }
    }
    Timer { interval: 2000; repeat: false; running: true; onTriggered: Qt.exit(12) }
}

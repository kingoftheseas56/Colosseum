import QtQuick
import "../qml/CatalogDelivery.js" as Delivery
import "../qml/TankobanDiscoverApi.js" as DiscoverApi
import "../qml/TheatreApi.js" as TheatreApi
Item {
    id: root
    property int failures: 0
    function check(ok, why) { if (!ok) { failures++; console.error(why); } }
    QtObject {
        id: catalog
        property bool remoteEnabled: true
        property int revision: 0
        property bool pending: false
        property var value: null
        signal requestFailed(string method, string reason)
        function available() { return true }
        function ready() { return false }
        function remotePending() { return pending }
        function discoverPageFiltered(cat, filters, explicitRows, offset, limit) {
            if (value === null) { pending = true; return ({}) }
            return value
        }
        function titleCatalog(query, offset, limit) {
            if (value === null) { pending = true; return [] }
            return value
        }
        function genreEntries(medium, genre, order, limit) {
            if (value === null) { pending = true; return [] }
            return value
        }
        function complete(result) { value = result; pending = false; revision++ }
        function discoverFilters(axis, explicitRows) { return [] }
    }
    Component.onCompleted: {
        var count = 0
        var adapter = DiscoverApi.create(catalog, null, [], false, function() { throw new Error("no live query in filtered harness"); })
        var state = {type:"manga", catalogKey:"popular", filters:{Genres:"action"}, filterGroup:"Genres", filterKey:"action"}
        var cancel = adapter.fetchPage(state, 24, 7, function(generation, page) {
            count++
            check(generation === 7, "Discover generation preserved")
            check(page.items.length === 1, "Discover receives API row")
            check(page.nextCursor === 25 && page.exhausted === false, "Discover preserves API pagination")
        })
        check(count === 0 && catalog.pending, "cold cache must not settle exhausted")
        catalog.complete({items:[{mal_id:1,title:"Series",type:"Manga",explicit:false,availability:false}], nextOffset:25,exhausted:false,freshness:"bundled"})
        check(count === 1, "Discover finishes exactly once")
        catalog.revision++
        check(count === 1, "settled callback disconnected")
        catalog.value = null
        var cancelledCalls = 0
        var stop = adapter.fetchPage(state, 48, 8, function() { cancelledCalls++ })
        check(typeof stop === "function", "adapter returns cancellation owner")
        stop()
        catalog.complete({items:[],nextOffset:48,exhausted:true})
        check(cancelledCalls === 0, "cancelled page cannot paint")
        catalog.value = null
        var theatreCalls = 0
        TheatreApi.loadRowPage({pageKey:"movies", sourceKind:"house", rowKey:"hidden-gems"},0,40,
            {imdbCatalog:catalog,generation:9}, function(page) {
                theatreCalls++
                check(page.generation === 9 && page.items.length === 1, "See-all receives asynchronous index result")
            })
        check(theatreCalls === 0, "See-all waits rather than marking empty")
        catalog.complete([{tt:"tt001",type:"movie",title:"Movie",year:2000,rating:8,votes:100,genres:[]}])
        check(theatreCalls === 1, "See-all completes exactly once")
        catalog.value = null
        var relatedCalls = 0
        var cancelRelated = TheatreApi.moreLikeThis("series", "mal:1", "tt001", "Action", catalog, function(cards) {
            relatedCalls++
            check(cards.length === 1 && cards[0].id === "mal:2", "async similar titles preserve identity exclusion")
        })
        check(relatedCalls === 0, "similar titles wait for semantic genre result")
        catalog.complete([{mal_id:1,title:"Self"},{mal_id:2,title:"Related"}])
        check(relatedCalls === 1, "similar titles complete once")
        if (!failures) console.log("CATALOG_SEMANTIC_ASYNC_OK")
        Qt.exit(failures ? 1 : 0)
    }
    Timer { interval: 10000; running: true; onTriggered: { console.error("timeout"); Qt.exit(1) } }
}

// Complete Tankoban wrapper + native-shaped per-query mock. No Main or network.
import QtQuick
import "../qml" as UI

Item {
    id: root
    width: 1200; height: 720
    property int phase: 0
    property var failures: []
    property int assertions: 0
    property var results: ({})
    function check(ok, label) {
        assertions++
        if (!ok) { failures.push(label); console.warn("CORRELATION_ASSERT_FAIL " + label) }
    }
    function snapshot(name) {
        var b = pageHost._shellForTest
        results[name] = {type:b.currentType,catalog:b.currentCatalogKey,
            generation:b.fetchGen,items:b.items.length,cursor:b.cursor,
            exhausted:b.exhausted,failed:b.pageFailed,loading:b.loading,
            filter:b.automationFilterSummary,warning:b.warning}
        console.warn("CORRELATION_STATE " + name + " " + JSON.stringify(results[name]))
    }
    function finish() {
        console.warn("CORRELATION_ASSERT_RESULT " + JSON.stringify({
            assertions:assertions, failures:failures, results:results}))
        console.warn(failures.length ? "CORRELATION_QML_FAIL" : "CORRELATION_QML_PASS")
        Qt.exit(failures.length ? 9 : 0)
    }
    component NativeCatalogue: QtObject {
        property string cardType: "manga"
        property bool remoteEnabled: true
        property var entries: ({})
        signal remoteQueryChanged(string key)
        signal revisionChanged()
        signal readyChanged()
        signal requestFailed(string method, string error)
        signal filterCacheReady()
        function ready() { return true }
        function available() { return true }
        function remotePending() { return false }
        function remoteWaiting() { return false }
        function discoverFilters(axis, explicit) {
            if (cardType === "manga" && axis === "genre") return [{value:"Action", count:9}]
            if (cardType === "comics" && axis === "publisher") return [{key:"marvel",label:"Marvel",count:9}]
            return []
        }
        function cachedDiscoverFilters(axis, explicit) { return discoverFilters(axis, explicit) }
        function addr(cat, filters, offset) {
            var a=[]
            for (var i=0; i<filters.length; i++) a.push(filters[i].axis + "=" + filters[i].key)
            return cat + "|" + a.sort().join(";") + "|" + offset
        }
        function entry(cat, filters, offset) {
            var addrKey=addr(cat,filters,offset)
            if (!entries[addrKey])
                entries[addrKey]={key:"NATIVE-UNSET-"+cardType, page:null, pending:false, failed:false, stale:false, error:""}
            return entries[addrKey]
        }
        function prime(cat, filters, offset, nativeKey, cached, stale) {
            entries[addr(cat,filters,offset)] = {key:nativeKey,page:cached||null,
                pending:false,failed:false,stale:!!stale,error:""}
        }
        function rows(n, offset) {
            var a=[]
            for(var i=0; i<n; i++) {
                var id=offset+i+1
                if (cardType==="manga") a.push({mal_id:id,title:"Manga "+id,explicit:false})
                else a.push({locgId:"comic:"+id,title:"Comic "+id,publisher:"Marvel",explicit:false})
            }
            return a
        }
        function makePage(n, offset, last) {
            return {items:rows(n,offset),nextOffset:last ? offset+n : offset+24,exhausted:last}
        }
        function discoverPageFiltered(cat,filters,explicit,offset,limit) {
            var e=entry(cat,filters,offset)
            if ((!e.page || e.stale) && !e.failed) e.pending=true
            return e.page || {items:[],nextOffset:offset,exhausted:true}
        }
        function discoverPageFilteredState(cat,filters,explicit,offset,limit) {
            var e=entry(cat,filters,offset)
            return {source:"remote",key:e.key,enabled:true, cached:e.page!==null,
                    status:e.pending?"pending":e.failed?"failure":e.page?"success":"missing",
                    error:e.error,retryable:e.failed && !e.pending}
        }
        function retryDiscoverPageFiltered(cat,filters,explicit,offset,limit) {
            var e=entry(cat,filters,offset)
            if (!e.failed) return false
            e.failed=false; e.pending=true
            return true
        }
        function settle(cat,filters,offset,n,last) {
            var e=entry(cat,filters,offset)
            e.page=makePage(n,offset,last)
            e.stale=false; e.pending=false; e.failed=false
            remoteQueryChanged(e.key)
            revisionChanged()
        }
        function fail(cat,filters,offset,reason) {
            var e=entry(cat,filters,offset)
            e.pending=false; e.failed=true; e.error=reason
            remoteQueryChanged(e.key)
            requestFailed("discoverPageFiltered",reason)
        }
        function invalidateKey(cat,filters,offset,replacement) {
            var e=entry(cat,filters,offset)
            e.key=replacement
            revisionChanged()
        }
    }
    NativeCatalogue { id: mal; cardType: "manga" }
    NativeCatalogue { id: comics; cardType: "comics" }
    UI.TankobanDiscoverPage {
        id: pageHost; width: 950; height: 720
        malCatalog: mal; comicsCatalog: comics
        _xhrFactory: function() { return {
            open:function(){}, send:function(){}, setRequestHeader:function(){}
        }}
    }
    property var biblioAdapter: ({
        types:function(){ return [{key:"book",label:"Book"}] },
        catalogs:function(t){ return [{key:"popular",title:"Popular",sourceKind:"builtin",sourceName:"Local"}] },
        defaultCatalog:function(){ return "popular" },
        filters:function(){ return [] },
        fetchPage:function(s,c,g,done){ done(g,{items:[{id:"local",title:"Local book",type:"book"}],
            nextCursor:null,exhausted:true,freshness:"local",warning:""}) }
    })
    UI.DiscoverBrowser {
        id: local; x: 960; width: 220; height: 300
        automationPrefix: "biblio"; active: false
        adapter: root.biblioAdapter
    }
    Timer { interval: 90; running: true; repeat: true; onTriggered: root.tick() }
    Timer { interval: 25000; running: true; repeat:false;
        onTriggered: { console.warn("CORRELATION_TIMEOUT"); Qt.exit(88) } }
    function tick() {
        var b=pageHost._shellForTest
        var pub=[{axis:"publisher",key:"marvel"}]
        if (phase===0) {
            mal.prime("popular",[],0,"NATIVE-MANGA-A")
            pageHost.applyPin({type:"manga",catalogId:"popular"})
            check(b.loading,"manga query queued")
        } else if (phase===1) {
            mal.settle("popular",[],0,24,false)
            check(b.items.length===24 && b.cursor===24,"manga rows and cursor accepted")
            snapshot("manga-first")
        } else if (phase===2) {
            mal.prime("popular",[],24,"NATIVE-MANGA-B")
            b.requestPage()
            check(b.loading,"manga pagination pending")
        } else if (phase===3) {
            mal.prime("top-rated",[],0,"NATIVE-MANGA-C")
            b.selectCatalog("top-rated")
            var gen=b.fetchGen
            mal.settle("popular",[],24,5,true)
            check(b.fetchGen===gen && b.items.length===0 && b.loading,
                  "late old generation not painted over new catalogue")
            snapshot("supersede-old")
        } else if (phase===4) {
            mal.settle("top-rated",[],0,3,true)
            check(b.items.length===3 && b.currentCatalogKey==="top-rated","new manga key accepted")
            snapshot("manga-new")
        } else if (phase===5) {
            comics.prime("popular",[],0,"NATIVE-COMICS-A")
            pageHost.applyPin({type:"comics",catalogId:"popular"})
            check(b.currentType==="comics" && b.loading,"comics switch queued")
        } else if (phase===6) {
            comics.settle("popular",[],0,24,false)
            check(b.items.length===24 && b.cursor===24,"comics first page accepted")
            snapshot("comics-first")
        } else if (phase===7) {
            comics.prime("popular",[],24,"NATIVE-COMICS-FAIL")
            b.requestPage()
            check(b.loading,"comics second page queued")
        } else if (phase===8) {
            comics.fail("popular",[],24,"HTTP_503")
            check(b.pageFailed && b.items.length===24 && b.cursor===24,
                "failed page preserves existing rows and cursor")
            snapshot("comics-failed")
            b.retryPage()
            check(b.loading && !b.pageFailed,"explicit retry requeued")
        } else if (phase===9) {
            comics.settle("popular",[],24,3,true)
            check(b.items.length===27 && b.exhausted && !b.pageFailed,"retry succeeds, appends")
            snapshot("comics-retried")
        } else if (phase===10) {
            comics.prime("popular",pub,0,"NATIVE-COMICS-FILTER")
            b.setFilter("Publishers","marvel")
            check(b.automationFilterSummary==="Publishers=marvel" && b.loading,"filter resets page")
        } else if (phase===11) {
            comics.settle("popular",pub,0,2,true)
            check(b.items.length===2 && b.items[0].type==="comics","filtered result accepted")
            snapshot("comics-filter")
        } else if (phase===12) {
            comics.prime("recently-available",[],0,"NATIVE-COMICS-STALE",
                comics.makePage(4,0,true),true)
            b.selectCatalog("recently-available")
            check(b.loading && b.items.length===0,"cached refresh pending, no early paint")
        } else if (phase===13) {
            comics.fail("recently-available",[],0,"HTTP_502")
            check(b.items.length===4 && b.warning==="Showing offline catalogue" && !b.pageFailed,
                "validated cached stale rows accepted with offline warning")
            snapshot("comics-stale")
        } else if (phase===14) {
            comics.prime("complete-runs",[],0,"NATIVE-COMICS-EMPTY")
            b.selectCatalog("complete-runs")
            check(b.loading,"empty query pending")
        } else if (phase===15) {
            comics.settle("complete-runs",[],0,0,true)
            check(b.items.length===0 && b.exhausted && !b.pageFailed,
                "valid empty is successful exhausted page")
            snapshot("valid-empty")
        } else if (phase===16) {
            comics.prime("new-releases",[],0,"NATIVE-SOURCE-BEFORE")
            b.selectCatalog("new-releases")
            check(b.loading,"source-reset test pending")
        } else if (phase===17) {
            comics.invalidateKey("new-releases",[],0,"NATIVE-SOURCE-AFTER")
            check(b.pageFailed && b.items.length===0,
                "native source-key change rejected, not successful model")
            snapshot("source-invalidated")
        } else if (phase===18) {
            comics.prime("most-stocked",[],0,"NATIVE-COMICS-RESET")
            b.selectCatalog("most-stocked")
            check(b.loading,"source reset new query pending")
        } else if (phase===19) {
            comics.settle("most-stocked",[],0,1,true)
            check(b.items.length===1,"source reset accepted correct new key")
            snapshot("reset-new")
        } else if (phase===20) {
            comics.prime("most-stocked",pub,0,"NATIVE-STALE-FILTER")
            b.setFilter("Publishers","marvel")
            check(b.loading,"late filter queued")
            b.clearFilters()
            var gen2=b.fetchGen
            comics.settle("most-stocked",pub,0,3,true)
            check(b.fetchGen===gen2 && b.automationFilterSummary==="" && b.items.length===1,
                "clear filter invalidates old response, cached current result retained")
            snapshot("filter-cleared")
        } else if (phase===21) {
            pageHost.active=false; pageHost.active=true
            check(b.items.length===1 && b.currentCatalogKey==="most-stocked",
                "hidden-world resume keeps accepted rows")
            local.active=true
            check(local.items.length===1,"local Biblio browser accepts row without native key")
            snapshot("local-unassigned")
        } else {
            finish()
        }
        phase++
    }
}

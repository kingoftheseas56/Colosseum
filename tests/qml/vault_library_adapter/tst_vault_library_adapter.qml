import QtQuick
import QtTest
import "../../../qml" as App

TestCase {
    id: suite
    name: "VaultLibraryAdapterUpdates"
    when: windowShown
    property var adapter
    property var library
    property var progress
    property var service
    Component {
        id: libraryComponent
        QtObject {
            signal changed()
            signal personalPathsChanged()
            signal browseArtResolved(string rowKey)
            signal browseArtworkReady(string rowKey, string coverUrl)
            property bool scanning: false
            property bool firstFolderIndexing: false
            property bool scanCanCancel: false
            property string scanPhase: "discovering"
            property int scanDone: 0
            property int cancelCalls: 0
            property string scanningRoot: "/private/stories"
            property var reads: ({})
            property string movieTitle: "A film"
            property string movieCover: "file:///initial.jpg"
            function cancelScan() { ++cancelCalls }
            function count(name) { reads[name] = (reads[name] || 0) + 1 }
            function rootsDetail() { count("roots"); return [{path:"/fixture",fileCount:2,itemCount:2,available:true}] }
            function series(kind) {
                count("series")
                return kind === "video" ? [{key:"film",kind:"video",identityId:"imdb:tt1234",identSource:"IMDB",identityTitle:movieTitle,identityType:"movie",identityYear:2020,coverUrl:movieCover,synopsis:"Synopsis"}]
                    : kind === "book" ? [{key:"book",kind:"book",identityId:"book:1",identityTitle:"A book",coverUrl:"file:///book.jpg"}] : []
            }
            function shelfItems(kind,key) {
                count("shelfItems")
                var rows = [{id:key+"-file",kind:kind,path:"/fixture/"+key,size:10,mtimeMs:1000,durationSec:600,
                             identityRating:8.2,identityGenres:"Drama",identityId:key==="film"?"imdb:tt1234":"book:1",
                             identityTitle:key==="film"?movieTitle:"A book"}]
                if (key === "film") rows.push({id:"extra-file",kind:kind,path:"/fixture/Extra",extra:true})
                return rows
            }
            function seasonFactsForShow(key) { count("seasons"); return {} }
            function browseDetail(key) { count("physical"); return {runtimeText:"10m",ratingText:"IMDb 8.2",genresLine:"Drama",copies:[{path:"/fixture/film"}]} }
            function presentationDetail(key) { count("presentation"); return {runtimeText:"10m",ratingText:"IMDb 8.2",genresLine:"Drama",copies:[{path:"/fixture/film"}]} }
            function hiddenSeries() { count("hidden"); return [] }
            function admissionById() { count("admission"); return {"film-file":"Admitted"} }
        }
    }
    Component {
        id: progressComponent
        QtObject {
            signal changed()
            property var records: ({})
            function get(kind,id) { return records[id] || null }
            function recent(kind,limit) { return [] }
            function watchedMark(id) { return 0 }
        }
    }
    Component {
        id: serviceComponent
        QtObject {
            signal changed()
            signal profileChanged()
            property bool busy: false
            property int profileGeneration: 1
            property int reads: 0
            property var serverTabs: [{id:"server",provider:"jellyfin",state:"ready",enabled:true}]
            function serverLibraries(id) { reads++; return [{id:"movies",titles:[{id:"tt5678",kind:"movie",title:"Remote film"}]}] }
        }
    }
    Component { id: adapterComponent; App.VaultLibraryAdapter {} }
    function init() {
        library = createTemporaryObject(libraryComponent,suite)
        progress = createTemporaryObject(progressComponent,suite)
        service = createTemporaryObject(serviceComponent,suite)
        adapter = createTemporaryObject(adapterComponent,suite,{library:library,progressStore:progress,homeServers:service})
        tryVerify(function() { return adapter.allItems.length === 2 })
        library.reads = ({}); service.reads = 0
    }
    function noLibraryReads() { compare(Object.keys(library.reads).length,0); compare(service.reads,0) }
    function test_indexingUsesNativeStateWithoutReprojectingShelves() {
        service.busy = true
        library.firstFolderIndexing = true
        compare(adapter.busy, true)
        compare(adapter.indexing, false)
        compare(adapter.firstFolderIndexing, false)
        compare(adapter.indexingStatus, "")
        service.busy = false
        library.changed()
        compare(adapter.busy, true)
        compare(adapter.indexing, false)
        tryCompare(adapter, "busy", false)
        library.reads = ({})

        library.scanning = true
        library.scanDone = 1
        library.scanCanCancel = true
        compare(adapter.indexing, true)
        compare(adapter.firstFolderIndexing, true)
        compare(adapter.indexingStatus, "Finding media · 1 file found")
        adapter.cancelScan()
        compare(library.cancelCalls, 1)
        library.scanPhase = "processing"
        library.scanDone = 14
        compare(adapter.indexingStatus, "Preparing files · 14 files prepared")
        library.scanPhase = "publishing"
        library.scanCanCancel = false
        compare(adapter.indexingStatus, "Updating shelves")
        adapter.cancelScan()
        compare(library.cancelCalls, 1)
        library.scanning = false
        compare(adapter.indexingStatus, "")
        noLibraryReads()
    }
    function test_artworkBurstPatchesOnlyFinalCover() {
        const books = adapter.books
        const unmatched = adapter.unmatchedItems
        const hidden = adapter.hiddenItems
        for (let i=0;i<20;++i) {
            library.browseArtResolved("film")
            library.browseArtworkReady("film","file:///poster-"+i+".jpg")
        }
        tryVerify(function() { return adapter.movies[0].cover === "file:///poster-19.jpg" })
        noLibraryReads()
        compare(adapter.movies[0].runtimeText,"10m")
        compare(adapter.movies[0].imdbRating,8.2)
        compare(adapter.movies[0].genres,"Drama")
        compare(adapter.movies[0].identity.coverUrl,"file:///poster-19.jpg")
        compare(adapter.heroItems.find(row=>row.kind==="movie").cover,"file:///poster-19.jpg")
        verify(adapter.books === books)
        verify(adapter.unmatchedItems === unmatched)
        verify(adapter.hiddenItems === hidden)
    }
    function test_progressBurstRejoinsAndClearsDeletedProgress() {
        progress.records = {"book-file":{id:"book-file",progress:0.4,updatedAt:10,resume:{path:"/fixture/book",chapterTitle:"Chapter 2"}}}
        for(let i=0;i<20;++i) progress.changed()
        tryVerify(function() { return adapter.books[0].hasProgress === true })
        noLibraryReads()
        compare(adapter.continueItems[0].title,"A book")
        compare(adapter.books[0].progressFraction,0.4)
        progress.records=({}); progress.changed()
        tryVerify(function() { return adapter.books[0].hasProgress === false })
        compare(adapter.books[0].progressFraction,0)
        compare(adapter.books[0].resumePath,"")
        compare(adapter.continueItems.length,0)
        noLibraryReads()
    }
    function test_structuralBurstIsCoalescedAndRetainsMetadata() {
        library.movieTitle="Updated film"
        for(let i=0;i<20;++i) library.changed()
        tryVerify(function() { return adapter.movies[0].title === "Updated film" })
        compare(library.reads.roots,1); compare(library.reads.series,3)
        compare(library.reads.shelfItems,2)
        compare(library.reads.seasons,undefined)
        compare(library.reads.presentation,undefined)
        compare(library.reads.physical,undefined)
        compare(adapter.movies[0].progressItems.length,1)
        compare(adapter.movies[0].imdbRating,8.2)
    }
    function test_artworkCannotPublishAheadOfPendingRevision() {
        library.movieTitle="Revised film"
        library.changed()
        library.browseArtworkReady("film","file:///obsolete.jpg")
        adapter.artworkTimer.triggered()
        compare(adapter.movies[0].cover,"file:///initial.jpg")
        compare(Object.keys(library.reads).length,0)
        tryVerify(function() { return adapter.movies[0].title==="Revised film" })
        compare(adapter.movies[0].cover,"file:///initial.jpg")
    }
    function test_personalChoicesRetireCachedArtworkBeforeRefresh() {
        compare(adapter.movies[0].cover, "file:///initial.jpg")
        library.browseArtworkReady("film", "file:///pending.jpg")
        library.movieCover = ""
        library.changed()
        library.personalPathsChanged()
        compare(adapter.movies.length, 0)
        compare(Object.keys(adapter.pendingArtwork).length, 0)
        tryVerify(function() { return adapter.movies.length === 1 })
        compare(adapter.movies[0].cover, "")
        compare(adapter.heroItems.find(row => row.kind === "movie").cover, "")
    }
    function test_profileSwitchRetiresRemoteRowsBeforeQueuedProgress() {
        progress.changed()
        service.serverTabs=[];service.profileGeneration++;service.profileChanged()
        tryVerify(function() { return adapter.servers.length===0 })
        compare(Object.keys(adapter.serverRows).length,0)
        noLibraryReads()
    }
    function test_replacementAndDestructionCancelQueuedWork() {
        library.browseArtworkReady("film","file:///obsolete.jpg")
        const replacement=createTemporaryObject(libraryComponent,suite,{movieTitle:"Replacement"})
        adapter.library=replacement
        tryVerify(function() { return adapter.movies[0].title==="Replacement" })
        compare(adapter.movies[0].cover,"file:///initial.jpg")
        replacement.reads=({})
        progress.changed();replacement.browseArtworkReady("film","file:///pending.jpg")
        adapter.destroy();adapter=null;wait(30)
        compare(Object.keys(replacement.reads).length,0)
    }
}

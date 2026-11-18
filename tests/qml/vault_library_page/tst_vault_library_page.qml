import QtQuick
import QtQuick.Controls
import QtTest
import "../../../qml" as App

// Declarative fixture only: no scanning, server requests, or writes to the user's library.
TestCase {
    id: suite
    name: "VaultLibraryPage"
    width: 1280; height: 700; visible: true
    when: windowShown
    property var page
    property var model
    property var film: ({id:"film", title:"Dunkirk", kind:"movie", copies:[],items:[], identified:true, year:2017, progressFraction:0.62, progressText:"40 min left", identity:{identityId:"imdb:tt5013056"}})
    property var book: ({id:"book", title:"Dune", kind:"book", copies:[],items:[], identified:true, path:"/fixture/dune.epub", year:1965})
    Component { id: adapterComponent; App.VaultLibraryAdapter { library: null; progressStore: null; homeServers: null } }
    Component {
        id: admissionLibraryComponent
        QtObject {
            property bool cardVisible: true
            property var candidate: [{subtreePath:"/fixture/new",groupTitle:"New books",kind:"book",count:2}]
            property string candidateRoot: "/fixture/new"
            property var identityCeremonies: []
            property int offered: 0
            property int degraded: 0
            property int confirmed: 0
            property int dismissed: 0
            property var attached: []
            property var supplementalByShow: null
            function offerUnconfirmedRoots() { offered++ }
            function rescanDegradedRoots() { degraded++ }
            function confirmRoot(path, overrides) { confirmed++; cardVisible=false }
            function dismissCard() { dismissed++; cardVisible=false }
            function attachExtrasToShow(from, to) { attached = [from, to]; return true }
            function extrasForShow(key) {
                return supplementalByShow
                    ? (supplementalByShow[key] || [])
                    : [{id:"supplement",title:"Featurette",path:"/fixture/extra.mkv",supplemental:true,away:false}]
            }
        }
    }
    Component {
        id: serverComponent
        QtObject {
            property int profileGeneration: 4
            property var serverTabs: []
            function serverLibraries(id) { return [] }
        }
    }
    Component { id: pageComponent; App.VaultLibraryPage { width:1280; height:700 } }
    Component {
        id: projectionLibraryComponent
        QtObject {
            signal changed()
            signal browseArtworkReady(string rowKey, string coverUrl)
            property int revision: 1
            property bool cardVisible: false
            property var identityCeremonies: []
            property var reads: ({})
            property var extrasReads: ({count:0})
            property var counts: [0,1,2]
            function count(name) { reads[name]=(reads[name] || 0)+1 }
            function rootsDetail() { count("roots"); return [{path:"/fixture",available:true,fileCount:3,itemCount:3}] }
            function series(kind) {
                count("series")
                return kind === "video" ? counts.map(function(n,i) {
                    return {key:"show-"+i,kind:"video",identityId:"imdb:tt"+(1000+i),identityTitle:"Show "+i,
                        identityType:"series",supplementalCount:n,coverUrl:"file:///show.jpg"}
                }) : []
            }
            function items(kind,key) { count("items"); return [{id:key+"-file",kind:kind,path:"/fixture/"+key}] }
            function hiddenSeries() { count("hidden"); return [] }
            function admissionById() { count("admission"); return {} }
            function seasonFactsForShow(key) { count("seasons"); return {} }
            function presentationDetail(key) { count("presentation"); return {} }
            function extrasForShow(key) {
                extrasReads.count++
                const n=counts[Number(key.split("-").pop())]
                return Array.from({length:n},function(_,i) { return {id:key+"-extra-"+i,title:"Featurette "+i,path:"/fixture/extra-"+i+".mkv",supplemental:true} })
            }
        }
    }
    Component {
        id: progressComponent
        QtObject {
            signal changed()
            function get(kind,id) { return null }
            function recent(kind,limit) { return [] }
            function watchedMark(id) { return 0 }
        }
    }
    SignalSpy { id: playback; signalName:"openMediaRequested" }
    SignalSpy { id: world; signalName:"viewWorldRequested" }
    SignalSpy { id: addFolder; signalName:"addFolderRequested" }
    SignalSpy { id: connect; signalName:"connectServerRequested" }
    function init() {
        model = createTemporaryObject(adapterComponent, suite)
        model.movies = [film]
        model.shows = [{id:"show",title:"The Wire",kind:"show",copies:[],items:[], identified:true}]
        model.books = [book]
        model.comics = [{id:"comic",title:"Saga",kind:"comic",copies:[],items:[], identified:true,path:"/fixture/saga.cbz"}]
        model.allItems = model.movies.concat(model.shows, model.books, model.comics)
        model.heroItems = [film]; model.continueItems = [film]
        model.roots = [{name:"Stories",path:"/fixture/media",subtitle:"4 titles · 12 files",state:"Watching",available:true}]
        model.servers = [{id:"offline",name:"Family Jellyfin",provider:"jellyfin",count:1,offline:true}]
        model.serverRows = {offline:[{id:"remote",title:"Arrival",kind:"movie",serverId:"offline",copies:[],items:[],offline:true}]}
        model.unmatchedItems = [{key:"unmatched",seriesKey:"unmatched",path:"/fixture/unknown-video.mkv",title:"Unidentified video",kind:"movie",nativeKind:"video",subtitle:"1 video"}]
        model.hiddenItems = [{key:"hidden",items:[],copies:[],title:"Hidden story",kind:"book",path:"/fixture/hidden.epub"}]
        page = createTemporaryObject(pageComponent, suite, {adapter:model})
        verify(page !== null)
        playback.target = page; world.target = page; connect.target = page; addFolder.target = page
        wait(260)
    }
    function cleanup() { playback.clear(); world.clear(); connect.clear(); addFolder.clear(); page.destroy(); wait(0) }
    function content() { return findChild(page,"vaultLibraryContent").item }
    function countMatchButtons(item) {
        if (item.objectName === "vaultMatchTitle") return 1
        return (item.children || []).reduce((count, child) => count + countMatchButtons(child), 0)
    }
    function test_largeUnmatchedListKeepsOnlyNearbyRows() {
        model.unmatchedItems = Array.from({length: 158}, (_, index) => ({
            seriesKey: "fixture-" + index, path: "/fixture/title-" + index + ".mkv",
            nativeKind: "video"
        }))
        page.showView("match", "")
        tryVerify(() => !!findChild(content(), "vaultMatchTitle"))
        tryVerify(() => countMatchButtons(content()) < 30)

        const firstButton = findChild(content(), "vaultMatchTitle")
        const rows = model.unmatchedItems.slice()
        rows[100] = Object.assign({}, rows[100], {cover: "file:///fixture/new-cover.jpg"})
        model.unmatchedItems = rows
        wait(0)
        compare(findChild(content(), "vaultMatchTitle"), firstButton)

        content().positionViewAtEnd()
        tryVerify(() => content().itemAtIndex(157) !== null)
        const lastButton = findChild(content().itemAtIndex(157), "vaultMatchTitle")
        verify(lastButton.visible)
        mouseClick(lastButton)
        tryCompare(findChild(page, "vaultIdentifyDialog"), "groupKey", "fixture-157")
    }
    function test_navigationAllViewsAndEscape() {
        compare(page.currentView,"home")
        compare(content().heroRows[0].title,"Dunkirk")
        const views = ["home","movies","shows","books","comics","match"]
        for (let i = 0; i < views.length; ++i) {
            const destination=findChild(page,"vaultNav_"+views[i])
            verify(destination.visible)
            mouseClick(destination)
            compare(page.currentView,views[i])
            if (i > 0 && i < 5) { compare(content().rows.length,1); verify(content().rows[0].identified) }
            page.forceActiveFocus(); keyClick(Qt.Key_Escape)
            compare(page.currentView,"home")
        }
        mouseClick(findChild(page,"vaulthiddenTile"))
        compare(page.currentView,"hidden")
        page.forceActiveFocus(); keyClick(Qt.Key_Escape); compare(page.currentView,"home")
    }
    function test_sourcesRepeatedOpenEscapeAndScrim() {
        const tile = findChild(page,"vaultsourcesTile")
        const sheet = findChild(page,"vaultStorageSheet")
        for (let i=0;i<2;++i) {
            mouseClick(tile); compare(page.sourcesOpen,true); wait(260)
            const panel = findChild(page,"vaultSourcesPanel")
            verify(panel.x >= 0 && panel.x+panel.width <= page.width)
            verify(panel.y+panel.height <= page.height)
            compare(panel.track, panel.x) // The blur crop follows the sliding panel.
            compare(findChild(page,"vaultSourcesFolderList").count,1)
            keyClick(Qt.Key_Escape); compare(page.sourcesOpen,false); wait(260)
        }
        mouseClick(tile); wait(260); mouseClick(sheet,300,300)
        compare(page.sourcesOpen,false)
    }
    function test_offlineServerAndOpenDestinations() {
        page.showView("server","offline"); wait(260)
        compare(content().server.offline,true)
        compare(content().rows[0].title,"Arrival")
        page.openRow(content().rows[0]); verify(page.notice.indexOf("Reconnect") >= 0)
        compare(world.count,0)
        page.openRow(book); compare(playback.count,1); compare(playback.signalArguments[0][0],"/fixture/dune.epub")
        page.openRow(film); compare(world.count,1)
        content().reconnectRequested(); compare(page.providerConnectionOpen,true)
        compare(page.connectionProvider,"jellyfin")
        page.handleBack(); compare(page.providerConnectionOpen,false)
    }
    function test_emptyAndSourceBounds() {
        model.movies=[]; model.shows=[]; model.books=[]; model.comics=[]; model.allItems=[]
        model.roots=[]; model.servers=[]; model.unmatchedItems=[]; model.heroItems=[]; model.continueItems=[]
        tryCompare(page,"vaultState","empty")
        const sidebar=findChild(page,"vaultLibrarySidebar")
        const pane=findChild(page,"vaultLibraryContent")
        compare(sidebar.y,68); compare(sidebar.y+sidebar.height,696)
        verify(sidebar.x+sidebar.width < pane.x)
        verify(pane.x+pane.width <= 1240)
        verify(pane.y+pane.height <= 676)
        verify(content().heroRows === undefined)
    }
    function findButton(item, text) {
        if (item.text === text && typeof item.clicked === "function") return item
        for (let i=0; i<item.children.length; ++i) {
            let hit=findButton(item.children[i],text)
            if (hit) return hit
        }
        return null
    }
    function test_sourceAddConnectAndIgnoreCancel() {
        mouseClick(findChild(page,"vaultsourcesTile")); wait(260)
        const sheet=findChild(page,"vaultStorageSheet")
        mouseClick(findButton(sheet,"+ Add folder")); compare(addFolder.count,1)
        mouseClick(findButton(sheet,"Ignore patterns ›")); compare(sheet.editingIgnore,true)
        const input=findChild(page,"vaultIgnorePatterns")
        input.text="*.tmp"
        mouseClick(findButton(sheet,"Cancel")); compare(sheet.editingIgnore,false)
        mouseClick(findButton(sheet,"Connect a server ›")); compare(connect.count,1); compare(page.sourcesOpen,false)
    }
    function test_forgetDialogEscapeStaysInSources() {
        mouseClick(findChild(page,"vaultsourcesTile")); wait(260)
        const sheet=findChild(page,"vaultStorageSheet")
        const dialog=findChild(page,"vaultForgetSourceDialog")
        dialog.path="/fixture/media"; dialog.open(); wait(30)
        verify(dialog.opened)
        keyClick(Qt.Key_Escape); wait(30)
        compare(dialog.opened,false); compare(page.sourcesOpen,true)
        keyClick(Qt.Key_Escape); compare(page.sourcesOpen,false)
        sheet.show(null); wait(260); dialog.open(); wait(30)
        sheet.open=false; wait(30); compare(dialog.opened,false)
    }
    function test_folderAdmissionConfirmationAndDismiss() {
        page.destroy(); wait(0)
        const library=createTemporaryObject(admissionLibraryComponent,suite)
        model.library=library
        page=createTemporaryObject(pageComponent,suite,{adapter:model})
        wait(40)
        compare(library.offered,1); compare(library.degraded,1)
        compare(page.cardVisible,true)
        const card=findChild(page,"vaultCard")
        compare(card.sliceCount,1)
        card.shelveRequested({}); compare(library.confirmed,1); compare(page.cardVisible,false)
        library.cardVisible=true
        page.handleBack(); compare(library.dismissed,1); compare(page.cardVisible,false)
    }
    function test_matchSearchPrefersFilenameStem() {
        const dialog = findChild(page, "vaultIdentifyDialog")
        verify(dialog !== null)
        var queries = []
        dialog.searchProvider = { search: function(query, kind, callback) {
            queries.push(query)
            return []
        } }
        page.matchRow({seriesKey:"/fixture/vault",nativeKind:"video",kind:"movie",
            path:"/fixture/vault/qualification-unmatched-xyz-4827.mp4",payload:{title:"vault"}})
        tryCompare(dialog, "opened", true)
        compare(dialog.titleText, "qualification-unmatched-xyz-4827")
        compare(dialog.query, "qualification-unmatched-xyz-4827")
        verify(queries.length > 0)
        compare(queries[queries.length - 1], "qualification-unmatched-xyz-4827")
        dialog.close(); tryCompare(dialog, "opened", false)
        page.matchRow({seriesKey:"group",nativeKind:"video",kind:"movie",payload:{title:"vault"},
            items:[{path:"C:\\Media\\Film.Name.2024.mkv"}]})
        tryCompare(dialog, "opened", true)
        compare(dialog.query, "Film.Name.2024")
        dialog.close(); tryCompare(dialog, "opened", false)
        page.matchRow({seriesKey:"group",nativeKind:"video",kind:"movie",payload:{title:"Known group"}})
        tryCompare(dialog, "opened", true)
        compare(dialog.query, "Known group")
        dialog.close()
    }
    function test_canonicalServerResumeContext() {
        model.homeServers=createTemporaryObject(serverComponent,suite)
        page.resumeRow({serverId:"server",profileGeneration:4,kind:"show",title:"Show",identity:{world:"Theatre",identityId:"tmdb:tv:123"},
            progressRecord:{id:"tmdb:tv:123:2:3",resume:{position:45}}})
        compare(world.count,1)
        var context=world.signalArguments[0][0].resumeSources.context
        compare(context.tmdbId,123); compare(context.season,2); compare(context.episode,3)
        verify(context.imdbId === undefined); compare(context.resumePosition,45)
        page.resumeRow({serverId:"server",profileGeneration:3,kind:"show",identity:{identityId:"tt1"},progressRecord:{id:"tt1:1:1"}})
        compare(world.count,1)
    }
    function test_extrasExplicitTargetAndSupplementalOpen() {
        const library=createTemporaryObject(admissionLibraryComponent,suite,{cardVisible:false})
        model.library=library
        model.shows=[{seriesKey:"show-group",kind:"show",title:"The Wire"}]
        page.fileExtras({seriesKey:"extra-group",nativeKind:"video"})
        const dialog=findChild(page,"vaultExtrasDialog")
        tryCompare(dialog,"opened",true)
        mouseClick(findButton(dialog.contentItem,"The Wire"))
        compare(library.attached.length,2); compare(library.attached[0],"extra-group"); compare(library.attached[1],"show-group")
        compare(dialog.supplemental.length,1); verify(dialog.supplemental[0].supplemental)
        mouseClick(findButton(dialog.contentItem,"Featurette"))
        compare(playback.count,1); compare(playback.signalArguments[0][0],"/fixture/extra.mkv")
        page.handleBack(); tryCompare(dialog,"opened",false)
    }
    function test_onlyShowsWithAttachedExtrasHaveAnExtrasEntry() {
        const library=createTemporaryObject(admissionLibraryComponent,suite,{cardVisible:false})
        library.supplementalByShow={
            "show-empty":[],
            "show-attached":[{id:"supplement",title:"Featurette",path:"/fixture/extra.mkv",supplemental:true,away:false}]
        }
        model.library=library
        model.shows=[
            {id:"empty",seriesKey:"show-empty",title:"Empty Show",kind:"show",supplementalCount:0},
            {id:"attached",seriesKey:"show-attached",title:"The Wire",kind:"show",supplementalCount:1}
        ]
        page.showView("match","")
        verify(findChild(page,"vaultShowExtras_empty") === null,"Empty shows have no supplemental-files entry")
        const entry=findChild(page,"vaultShowExtras_attached")
        verify(!!entry,"The show with attached extras remains reachable")
        compare(entry.accessibleName,"1 supplemental file for The Wire")
        mouseClick(entry)
        const dialog=findChild(page,"vaultExtrasDialog")
        tryCompare(dialog,"opened",true)
        compare(dialog.supplemental.length,1)
        mouseClick(findButton(dialog.contentItem,"Featurette"))
        compare(playback.count,1)
        compare(playback.signalArguments[0][0],"/fixture/extra.mkv")
    }
    function test_matchViewNarrowUpdatesUseProjectedExtrasCounts() {
        const library=createTemporaryObject(projectionLibraryComponent,suite)
        const progress=createTemporaryObject(progressComponent,suite)
        model=createTemporaryObject(adapterComponent,suite,{library:library,progressStore:progress})
        page.adapter=model
        page.showView("match","")
        tryVerify(function() { return !!findChild(page,"vaultShowExtras_imdb:tt1001") })
        verify(findChild(page,"vaultShowExtras_imdb:tt1000") === null)
        compare(findChild(page,"vaultShowExtras_imdb:tt1002").accessibleName,"2 supplemental files for Show 2")
        library.reads=({});library.extrasReads.count=0
        for(let i=0;i<20;++i) {
            library.browseArtworkReady("show-1","file:///ready-"+i+".jpg")
            progress.changed()
        }
        tryVerify(function() { return model.shows[1].cover === "file:///ready-19.jpg" })
        compare(Object.keys(library.reads).length,0)
        compare(library.extrasReads.count,0)
        verify(!!findChild(page,"vaultShowExtras_imdb:tt1001"))
        verify(findChild(page,"vaultShowExtras_imdb:tt1000") === null)

        library.counts=[0,0,1];library.revision++;library.changed()
        tryVerify(function() { return findChild(page,"vaultShowExtras_imdb:tt1001") === null })
        compare(findChild(page,"vaultShowExtras_imdb:tt1002").accessibleName,"1 supplemental file for Show 2")
        compare(library.extrasReads.count,0)
        library.reads=({})
        progress.changed();library.browseArtworkReady("show-2","file:///remaining.jpg")
        tryVerify(function() { return model.shows[2].cover === "file:///remaining.jpg" })
        compare(Object.keys(library.reads).length,0)
        compare(library.extrasReads.count,0)
        mouseClick(findChild(page,"vaultShowExtras_imdb:tt1002"))
        const dialog=findChild(page,"vaultExtrasDialog")
        tryCompare(dialog,"opened",true)
        compare(library.extrasReads.count,1)
        compare(dialog.supplemental.length,1)
        mouseClick(findButton(dialog.contentItem,"Featurette 0"))
        compare(playback.count,1)
        compare(playback.signalArguments[0][0],"/fixture/extra-0.mkv")
    }
    function test_hoverFocusAndPress() {
        const nav=findChild(page,"vaultNav_books")
        mouseMove(nav,nav.width/2,nav.height/2)
        tryCompare(nav,"hovered",true)
        nav.forceActiveFocus(Qt.TabFocusReason)
        verify(nav.activeFocus); verify(nav.visualFocus)
        mousePress(nav); verify(nav.down)
        mouseRelease(nav); compare(page.currentView,"books")
    }
}

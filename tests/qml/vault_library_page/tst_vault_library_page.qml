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
            function offerUnconfirmedRoots() { offered++ }
            function rescanDegradedRoots() { degraded++ }
            function confirmRoot(path, overrides) { confirmed++; cardVisible=false }
            function dismissCard() { dismissed++; cardVisible=false }
        }
    }
    Component { id: pageComponent; App.VaultLibraryPage { width:1280; height:700 } }
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
    function test_navigationAllViewsAndEscape() {
        compare(page.currentView,"home")
        compare(content().heroRows[0].title,"Dunkirk")
        const views = ["movies","shows","books","comics","match"]
        for (let i = 0; i < views.length; ++i) {
            mouseClick(findChild(page,"vaultNav_"+views[i]))
            compare(page.currentView,views[i])
            if (i < 4) { compare(content().rows.length,1); verify(content().rows[0].identified) }
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
        content().reconnectRequested(); compare(connect.count,1)
    }
    function test_emptyAndSourceBounds() {
        model.movies=[]; model.shows=[]; model.books=[]; model.comics=[]; model.allItems=[]
        model.roots=[]; model.servers=[]; model.unmatchedItems=[]; model.heroItems=[]; model.continueItems=[]
        tryCompare(page,"vaultState","empty")
        const sidebar=findChild(page,"vaultLibrarySidebar")
        const pane=findChild(page,"vaultLibraryContent")
        compare(sidebar.y,84); compare(sidebar.y+sidebar.height,604)
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

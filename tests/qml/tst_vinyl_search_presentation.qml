import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/vinyl" as Vinyl
TestCase {
    id: test
    name: "VinylSearchPresentation"
    when: windowShown
    visible: true
    width: 1280; height: 900
    property var opened: null
    QtObject {
        id: sessionStub
        property var source: ({catalogueSearchVersion:1})
        property string catalogueQuery: "needle"
        property string browseOperation: ""
        property bool browsing: false
        property var catalogueRequests: ({})
        property var requestCalls: []
        property var catalogueKinds: ["artists","songs","albums","playlists"]
        property var cataloguePages: ({artists:{items:[{id:"artist",title:"Artist",kind:"artists"}]},
            songs:{items:[{id:"song",title:"Song",artist:"Singer",subtitle:"Singer",kind:"songs"}]},
            albums:{items:[]},playlists:{items:[]}})
        function restoreCatalogue(value) {}
        function captureCatalogue() { return {}; }
        function cancelCatalogueRequests() {}
        function clearCatalogue() {}
        function requestCataloguePage(kind, more) { requestCalls=requestCalls.concat([{kind:kind,more:more}]); }
    }
    QtObject {
        id: worldStub
        property bool usesRecommendationService: false
        property var pickerDraft: []
        property bool artistSearchLoading: false
        property string artistSearchError: ""
        property bool artistSearchHasMore: false
        property int minimumArtistCount: 5
        property string onboardingState: "editing"
        function artistOptions(query) { return []; }
        property var searchHistoryStore: null
        property var session: sessionStub
        function artwork(value) { return value || ""; }
    }
    Component { id: vinylFactory; Vinyl.VinylSearchPage {
        width: 1280; height: 900; session: sessionStub; worldData: worldStub
        initialState: ({query:"needle",catalogue:{}})
        onItemRequested: data => test.opened=data
    } }
    Component { id: defaultFactory; App.SearchSurface {
        width: 1280; height: 900; historyStore: null
        onItemRequested: data => test.opened=data
    } }
    function init() {
        sessionStub.catalogueRequests=({});sessionStub.requestCalls=[];
        sessionStub.cataloguePages=({artists:{items:[{id:"artist",title:"Artist",kind:"artists"}]},
            songs:{items:[{id:"song",title:"Song",artist:"Singer",subtitle:"Singer",kind:"songs"}]},
            albums:{items:[]},playlists:{items:[]}});
    }
    function visibleTextCount(node,value) {
        var count=node.visible && node.text===value ? 1 : 0;
        for(var child of node.children||[])count+=visibleTextCount(child,value);
        return count;
    }
    function test_loadingFootprintsPartialErrorAndCompletion() {
        sessionStub.cataloguePages=({artists:{items:[]},songs:{items:[]},albums:{items:[]},playlists:{items:[]}});
        sessionStub.catalogueRequests=({artists:1,songs:2,albums:3,playlists:4});
        var vinyl=createTemporaryObject(vinylFactory,test);
        verify(vinyl!==null);compare(vinyl.resultCount,0);verify(vinyl.searching);
        var artist=findChild(vinyl,"searchLoading_artists_0");
        var song=findChild(vinyl,"searchLoading_songs_0");
        var album=findChild(vinyl,"searchLoading_albums_0");
        verify(artist!==null && song!==null && album!==null);
        tryCompare(song,"status",Loader.Ready);
        compare(artist.item.kind,"artists");compare(song.item.kind,"songs");
        verify(song.height>song.width);verify(artist.height>artist.width);
        var songMetrics=findChild(vinyl,"vinylSearchSongLoadingMetrics");
        compare(song.item.artworkHeight,songMetrics.implicitHeight);
        compare(songMetrics.width,song.width);
        verify(song.Accessible.ignored);
        compare(findChild(vinyl,"searchLoadingRow_songs").Accessible.name,"Loading Songs");
        compare(findChild(vinyl,"searchGroupHeading_artists").text,"ARTISTS");
        compare(visibleTextCount(vinyl,"Searching…"),0);
        verify(!vinyl.showingNoResults);
        sessionStub.cataloguePages=({artists:{items:[{id:"partial",kind:"artists",title:"Real artist"},
            {id:"partial-second",kind:"artists",title:"Another real artist"}]},
            songs:{items:[]},albums:{items:[],error:"Album search failed"},playlists:{items:[]}});
        sessionStub.catalogueRequests=({artists:1,songs:2,playlists:4});wait(20);
        compare(vinyl.resultCount,2);
        verify(!findChild(vinyl,"searchLoadingRow_artists").visible);
        compare(findChild(vinyl,"searchGroupHeading_artists").text,"ARTISTS  ·  1");
        compare(visibleTextCount(vinyl,"Searching…"),0);
        verify(findChild(vinyl,"searchLoading_artists_0")===null);
        verify(findChild(vinyl,"searchLoading_songs_0")!==null);
        var top=findChild(vinyl,"searchTopPresentation");tryCompare(top,"status",Loader.Ready);
        compare(top.item.item.id,"partial");
        var retry=findChild(vinyl,"searchMore_albums");verify(retry!==null && retry.enabled);
        verify(retry.activate(Qt.MouseFocusReason));compare(sessionStub.requestCalls[0].kind,"albums");
        compare(sessionStub.requestCalls[0].more,false);
        sessionStub.catalogueRequests=({});sessionStub.cataloguePages=({artists:{items:[]},songs:{items:[]},albums:{items:[]},playlists:{items:[]}});
        wait(20);verify(vinyl.showingNoResults);
        verify(findChild(vinyl,"searchLoading_songs_0")===null);
        vinyl.artistPicker=true;compare(vinyl.resultLoadingPresentation,null);
    }
    function test_defaultAndVinylPresentation() {
        var standard=createTemporaryObject(defaultFactory,test);
        verify(standard !== null);
        compare(standard.resultPresentation, null);
        compare(standard.resultLoadingPresentation,null);
        standard.results=[{title:"Default",data:{id:"default"}}];
        standard.restoreQuery("needle");
        var defaultTop=findChild(standard,"searchTopPresentation");
        verify(defaultTop !== null); verify(!defaultTop.active);
        standard.openTop(); compare(opened.id,"default");
        var vinyl=createTemporaryObject(vinylFactory,test);
        verify(vinyl !== null);
        var top=findChild(vinyl,"searchTopPresentation");
        tryCompare(top,"status",Loader.Ready);
        compare(top.item.presentationKind,"artists");
        verify(top.item.includesTitle);
        var song=findChild(vinyl,"searchGroupedPresentation_song");
        verify(song !== null); tryCompare(song,"status",Loader.Ready);
        compare(song.item.presentationKind,"songs");
        compare(song.item.artistText,"Singer"); compare(song.item.secondaryText,"");
        verify(song.height > song.width, "Song label must grow beyond its square cover");
        standard.visible=false;
        wait(50); vinyl.keepSearchDelegateVisible(song); wait(50);
        mouseClick(song, song.width / 2, 20); compare(opened.id,"song");
        vinyl.openTop(); compare(opened.id,"artist");
        sessionStub.cataloguePages=({artists:{items:[]},songs:{items:[{id:"song-top",kind:"songs",title:"Song top",artist:"Singer"}]},albums:{items:[]},playlists:{items:[]}});
        compare(top.item.presentationKind,"songs");
        verify(top.height > top.width, "Top song also owns its growing paper label");
        vinyl.openTop(); compare(opened.id,"song-top");
        vinyl.artistPicker=true;
        compare(vinyl.resultPresentation,null);
    }
}

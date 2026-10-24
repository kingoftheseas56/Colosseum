import QtQuick
import QtTest
import "../../qml/vinyl"

TestCase {
    id: test
    name: "VinylLiveWorldState"
    when: windowShown
    property var viewData
    QtObject {
        id: fakeSession
        property var library: []
        property var onlineAlbums: []
        property var onlineArtists: []
        property string browseError: ""
        property string browseOperation: ""
        property bool browsing: false
        property bool pagedSearchSupported: false
        property bool searchComplete: false
        property string searchQuery: ""
        property var results: []
        property int playingIndex: -1
        property var playingTrack: ({})
        property var playingEdition: ({})
        property int profileGeneration: 0
        property var searches: []
        function search(value) { searchQuery=value; searches=searches.concat([value]); }
        function searchCatalogue(value) { browseOperation="catalogueSearch"; browsing=true; searches=searches.concat([value]); }
        function cancelBrowse() { browsing=false; browseOperation=""; }
    }
    Component { id: factory; VinylLiveWorldData { session: fakeSession; preferencesStore: null; searchHistoryStore: null } }
    function album(id, title, origin) {
        return {id:id,title:title,origin:origin || "local",artists:["Artist"],artistIds:["artist"],year:2000,genre:"Jazz",tracks:[{id:id+"-song",title:"Song",artists:["Artist"],disc:1,number:1}]};
    }
    function init() {
        fakeSession.library=[{payload:{album:album("local-a","Alpha")}},{payload:{album:album("local-b","Beta")}}];
        fakeSession.onlineAlbums=[];fakeSession.onlineArtists=[];fakeSession.browseOperation="";fakeSession.browsing=false;
        fakeSession.searches=[];fakeSession.profileGeneration=0;fakeSession.browseError="";
        viewData=factory.createObject(test);
        verify(viewData!==null);
    }
    function cleanup() { viewData.destroy();wait(0); }
    function test_homeDoesNotUseActiveLibraryFilter() {
        viewData.query="missing";
        compare(viewData.row("library",false).items.length,0);
        compare(viewData.homeRows[0].items.length,2,"Home must use the complete library projection");
        compare(viewData.row(viewData.homeRows[0].key,true).items.length,2,"Home See all must retain its own source");
        compare(viewData.carouselSlides[0].title,"Alpha");
    }
    function test_eachTabRestoresOwnQueryKindSortAndMode() {
        viewData.query="Alpha";viewData.setKind("songs");viewData.sortOrder="year";
        viewData.selectTab("discover");compare(viewData.query,"");verify(viewData.onlineMode);
        viewData.setQuery("Remote");viewData.setKind("artists");viewData.sortOrder="title";
        viewData.selectTab("home");compare(viewData.query,"");verify(!viewData.onlineMode);
        viewData.selectTab("library");compare(viewData.query,"Alpha");compare(viewData.lens.kind,"songs");compare(viewData.sortOrder,"year");verify(!viewData.onlineMode);
        viewData.selectTab("discover");compare(viewData.query,"Remote");compare(viewData.lens.kind,"artists");compare(viewData.sortOrder,"title");verify(viewData.onlineMode);
    }
    function test_remoteResultsNeverReplaceHomeOrLibrary() {
        viewData.selectTab("discover");viewData.setQuery("Remote");viewData.submitSearch();
        fakeSession.onlineArtists=[{id:"remote-artist",name:"Remote Artist"}];fakeSession.onlineAlbums=[album("remote","Remote","youtube-music")];fakeSession.browsing=false;
        compare(viewData.albums[0].id,"remote");
        viewData.selectTab("home");compare(viewData.homeRows[0].items.length,2);
        viewData.selectTab("library");compare(viewData.row("library",false).items.length,2);verify(!viewData.onlineMode);
        compare(viewData.findAlbum("remote").id,"remote");
        viewData.selectTab("discover");compare(viewData.albums[0].id,"remote");
    }
    function test_changedOrClearedQueryDoesNotMislabelPreviousMatches() {
        viewData.selectTab("discover");viewData.query="Remote";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("remote","Remote","youtube-music")];fakeSession.browsing=false;
        compare(viewData.albums.length,1);
        viewData.query="Different";compare(viewData.albums.length,0);
        viewData.query="";compare(viewData.albums.length,0);
        viewData.query="Remote";compare(viewData.albums.length,1);
    }
    function test_cancelledSearchCannotRelabelOldResults() {
        viewData.selectTab("discover");viewData.query="One";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("one","One","youtube-music")];fakeSession.browsing=false;
        viewData.query="Two";viewData.submitSearch();viewData.selectTab("library");
        viewData.selectTab("discover");compare(viewData.query,"Two");compare(viewData.albums.length,0);
    }
    function test_profileResetClearsAllTabState() {
        viewData.query="Alpha";viewData.sortOrder="year";viewData.selectTab("discover");viewData.query="Remote";viewData.setKind("artists");
        fakeSession.profileGeneration++;
        compare(viewData.activeTab,"library");compare(viewData.query,"");compare(viewData.lens.kind,"albums");compare(viewData.sortOrder,"title");
        viewData.selectTab("discover");compare(viewData.query,"");compare(viewData.albums.length,0);compare(viewData.lens.kind,"albums");
    }
    function test_remoteSavedAlbumIsNotLabelledOnDevice() {
        fakeSession.library=[{payload:{album:album("saved","Saved remote","youtube-music")}}];
        compare(viewData.homeRows[0].title,"Your library");
        verify(viewData.carouselSlides.length===0 || viewData.carouselSlides[0].kicker!=="On this device");
    }
    function test_routeSnapshotRestoresOriginalSearchAndResults() {
        viewData.query="Alpha";viewData.selectTab("discover");viewData.query="One";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("one","One","youtube-music")];fakeSession.browsing=false;
        var snapshot=viewData.captureBrowseState();
        viewData.query="Two";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("two","Two","youtube-music")];fakeSession.browsing=false;
        compare(viewData.albums[0].id,"two");
        viewData.restoreBrowseState(snapshot);compare(viewData.query,"One");compare(viewData.albums[0].id,"one");
        viewData.selectTab("library");compare(viewData.query,"Alpha");
    }
    function test_localDiscoverDoesNotBorrowLibraryQuery() {
        viewData.query="Alpha";viewData.selectTab("discover");viewData.setOnlineMode(false);viewData.query="Beta";
        compare(viewData.albums[0].id,"local-b");compare(viewData.row("library",false).items[0].id,"local-a");
        viewData.selectTab("home");compare(viewData.homeRows[0].items.length,2);
        viewData.selectTab("discover");compare(viewData.query,"Beta");verify(!viewData.onlineMode);
    }
    function test_returnToLibraryDoesNotEraseNewDiscoverSearch() {
        viewData.query="Alpha";var libraryRoute=viewData.captureBrowseState();
        viewData.selectTab("discover");viewData.query="Remote";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("remote","Remote","youtube-music")];fakeSession.browsing=false;
        viewData.restoreBrowseState(libraryRoute);compare(viewData.query,"Alpha");
        viewData.selectTab("discover");compare(viewData.query,"Remote");compare(viewData.albums[0].id,"remote");
    }
}

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
        property real lastPlaybackAtMs: 0
        property string sourceState: "paused"
        property real duration: 0
        property real position: 0
        property bool paused: true
        property bool profileActive: true
        property bool shuttingDown: false
        property var searches: []
        function plainData(value) { return value === undefined ? null : JSON.parse(JSON.stringify(value)); }
        function search(value) { searchQuery=value; searches=searches.concat([value]); }
        function searchCatalogue(value) { browseOperation="catalogueSearch"; browsing=true; searches=searches.concat([value]); }
        function cancelBrowse() { browsing=false; browseOperation=""; }
    }
    QtObject {
        id: fakeRecommendationService
        property int minimumArtists: 5
        property var selectedArtists: []
        property var draftArtists: []
        property int beginTasteEditCalls: 0
        readonly property bool canEnterVinyl: selectedArtists.length >= minimumArtists
        property int homeRevision: 0
        property string homeStatus: "ready"
        property string homeError: ""
        property string error: ""
        property string onboardingState: "answered"
        property var homeSnapshot: ({historyEnabled:true,activityResetGeneration:0,historyResetGeneration:0})
        property var homeSections: []
        property var homeQuickPicks: []
        property var homeListeningCards: []
        property var homeCarouselItems: []
        property var artistResults: ({})
        function beginHomeVisit() {}
        function beginTasteEdit() {
            beginTasteEditCalls++;
            draftArtists=selectedArtists.slice();
            onboardingState="editing";
        }
        function refreshHomeSelection() {}
    }
    QtObject {
        id: fakePreferences
        property bool vinylTasteAnswered: true
        property var vinylTasteArtistIds: []
        function setVinylTasteSelections(answered, artists) { return true; }
    }
    Component {
        id: factory
        VinylLiveWorldData {
            session: fakeSession
            preferencesStore: fakePreferences
            recommendationService: fakeRecommendationService
            searchHistoryStore: null
        }
    }
    QtObject {
        id: fakeTasteService
        property int minimumArtists: 5
        property int draftArtistCount: 0
    }
    QtObject {
        id: fakePickerData
        property var recommendationService: fakeTasteService
        property bool usesRecommendationService: true
        property string onboardingState: "draft"
        property var pickerDraft: []
        property string pickerQuery: ""
        property string artistSearchError: ""
        property bool artistSearchLoading: false
        property bool artistSearchHasMore: false
        function artistOptions() { return []; }
        function artwork() { return ""; }
        function togglePickerArtist() {}
        function cancelTasteEdit() {}
        function answerPicker() {}
        function skipPicker() {}
        function retryArtistSearch() {}
        function loadMoreArtistSearch() {}
    }
    Item { id: fakeBackdrop; width: 640; height: 480 }
    Flickable { id: fakePageFlick; width: 640; height: 480 }
    Component {
        id: pickerFactory
        VinylTastePicker { worldData: fakePickerData; backdrop: fakeBackdrop; pageFlick: fakePageFlick }
    }
    function album(id, title, origin) {
        return {id:id,title:title,origin:origin || "local",artists:["Artist"],artistIds:["artist"],year:2000,genre:"Jazz",tracks:[{id:id+"-song",title:"Song",artists:["Artist"],disc:1,number:1}]};
    }
    function fixtureArtists() {
        return ["one","two","three","four","five"].map(function(id) {
            return {provider:"local",providerId:id,localId:id,name:"Artist "+id};
        });
    }
    function fixtureCarouselItems(count) {
        var result=[];
        for(var i=0;i<count;i++) result.push({key:"spotlight-"+(i+1),title:"Spotlight "+(i+1),
            subtitle:"Artist",cover:"",spotlightKind:"new-from-your-artists"});
        return result;
    }
    function init() {
        fakeSession.library=[{payload:{album:album("local-a","Alpha")}},{payload:{album:album("local-b","Beta")}}];
        fakeSession.onlineAlbums=[];fakeSession.onlineArtists=[];fakeSession.browseOperation="";fakeSession.browsing=false;
        fakeSession.searches=[];fakeSession.profileGeneration=0;fakeSession.browseError="";
        fakeSession.playingIndex=-1;fakeSession.playingTrack={};fakeSession.playingEdition={};
        fakeSession.lastPlaybackAtMs=0;fakeSession.sourceState="paused";fakeSession.duration=0;fakeSession.position=0;
        fakeRecommendationService.selectedArtists=fixtureArtists();
        fakeRecommendationService.draftArtists=[];fakeRecommendationService.beginTasteEditCalls=0;
        fakeRecommendationService.onboardingState="answered";
        fakeRecommendationService.homeQuickPicks=[{id:"quick-one",title:"Quick song",kind:"songs"}];
        fakeRecommendationService.homeListeningCards=[{id:"mix-one",title:"Daily mix",kind:"mixes"}];
        fakeRecommendationService.homeCarouselItems=fixtureCarouselItems(6);
        fakeTasteService.draftArtistCount=0;fakePickerData.pickerDraft=[];fakePickerData.pickerQuery="";
        viewData=factory.createObject(test);
        verify(viewData!==null);
    }
    function cleanup() { viewData.destroy();wait(0); }
    function test_homeDoesNotUseActiveLibraryFilter() {
        viewData.query="missing";
        compare(viewData.row("library",false).items.length,0);
        compare(viewData.homeRows.find(function(row){return row.key==="quick-picks";}).items[0].id,
            fakeRecommendationService.homeQuickPicks[0].id,"Home consumes the supplied quick picks");
        compare(viewData.homeRows.find(function(row){return row.key==="mixes-radio";}).items[0].id,
            fakeRecommendationService.homeListeningCards[0].id,"Home consumes the supplied listening cards");
        verify(!viewData.homeRows.some(function(row){return ["library","saved-albums","saved-songs","saved-artists"].indexOf(row.key)>=0;}));
        compare(viewData.catalogue.homeRegion.length,0,"Home has no regional catalogue feed");
        compare(viewData.carouselSlides[0].title,"Spotlight 1");
    }
    function test_artistPickerRequiresFiveAndHasNoSkip() {
        var picker=pickerFactory.createObject(test);
        verify(picker!==null);
        var done=findChild(picker,"vinylPickerDone");
        verify(done!==null);
        verify(!done.enabled);
        fakeTasteService.draftArtistCount=4;
        verify(!done.enabled);
        fakeTasteService.draftArtistCount=5;
        verify(done.enabled);
        compare(findChild(picker,"vinylPickerSkip"),null);
        picker.destroy();
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
        viewData.selectTab("home");compare(viewData.homeQuickPicks[0].id,"quick-one");
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
        fakeSession.library=[{id:"saved",payload:{album:album("saved","Saved remote","youtube-music"),saved:false}}];
        verify(!viewData.homeRows.some(function(row){return row.title==="Your library";}));
        compare(viewData.visibleLibraryAlbums.length,0,"An unsaved remote album is not a local library entry");
        compare(viewData.carouselSlides[0].title,"Spotlight 1");
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
        viewData.selectTab("home");compare(viewData.homeQuickPicks[0].id,"quick-one");
        viewData.selectTab("discover");compare(viewData.query,"Beta");verify(!viewData.onlineMode);
    }
    function test_returnToLibraryDoesNotEraseNewDiscoverSearch() {
        viewData.query="Alpha";var libraryRoute=viewData.captureBrowseState();
        viewData.selectTab("discover");viewData.query="Remote";viewData.submitSearch();
        fakeSession.onlineAlbums=[album("remote","Remote","youtube-music")];fakeSession.browsing=false;
        viewData.restoreBrowseState(libraryRoute);compare(viewData.query,"Alpha");
        viewData.selectTab("discover");compare(viewData.query,"Remote");compare(viewData.albums[0].id,"remote");
    }
    function test_homeGateAndRecommendationsUseSuppliedServiceData() {
        verify(viewData.canEnterVinyl);
        compare(viewData.homeQuickPicks[0].id,"quick-one");
        compare(viewData.homeListeningCards[0].id,"mix-one");
        verify(viewData.homeRows.some(function(row){return row.key==="taste-artists";}));
        fakeRecommendationService.selectedArtists=fixtureArtists().slice(0,4);
        verify(!viewData.canEnterVinyl,"four saved artists must not pass the five artist gate");
        compare(viewData.homeRows.length,0);
        compare(viewData.carouselSlides.length,0);
        fakeRecommendationService.selectedArtists=fixtureArtists();
        verify(viewData.canEnterVinyl);
    }
    function test_yourArtistsSeeAllOpensTasteEditorAndPreservesPicks() {
        viewData.selectTab("home");
        fakeRecommendationService.onboardingState="answered";
        fakeRecommendationService.beginTasteEditCalls=0;
        var saved=JSON.stringify(fakeRecommendationService.selectedArtists);
        viewData.artistSearchText="Bonobo";
        compare(viewData.pickerQuery,"Bonobo","the previous onboarding search is still present before editing");
        var focusRequests=0,listRequests=0;
        var onFocus=function(){focusRequests++;};
        var onList=function(){listRequests++;};
        viewData.artistSearchFocusRequested.connect(onFocus);
        viewData.listRequested.connect(onList);

        verify(viewData.homeRows.some(function(row){return row.key==="taste-artists";}));
        verify(viewData.openList("taste-artists",true));

        compare(fakeRecommendationService.beginTasteEditCalls,1);
        compare(viewData.onboardingState,"editing");
        verify(viewData.tastePickerOpen);
        compare(viewData.pickerQuery,"","a new taste edit starts with the fresh saved-artist suggestion query");
        compare(JSON.stringify(fakeRecommendationService.draftArtists),saved,
            "the existing taste editor starts from the saved artist choices");
        compare(JSON.stringify(fakeRecommendationService.selectedArtists),saved,
            "opening the editor does not mutate saved artist choices");
        compare(focusRequests,1,"the open-list path requests focus for the dedicated artist field");
        compare(listRequests,0,"Your artists See all does not enter the generic list route");

        viewData.artistSearchFocusRequested.disconnect(onFocus);
        viewData.listRequested.disconnect(onList);
    }
    function test_carouselUsesSuppliedItemsAndCapsAtFive() {
        fakeRecommendationService.homeCarouselItems=fixtureCarouselItems(8);
        compare(viewData.carouselSlides.length,5);
        compare(viewData.carouselSlides[0].id,"spotlight-1");
        compare(viewData.carouselSlides[4].id,"spotlight-5");
    }
    function test_resumeCarouselRequiresRecentPlaybackTimestamp() {
        var now=Date.now()+1000;
        viewData.homeNowMs=now;
        fakeSession.playingIndex=0;
        fakeSession.playingTrack={title:"Recent track"};
        fakeSession.playingEdition={artist:"Artist",cover:""};
        fakeSession.duration=180;
        fakeSession.position=30;
        fakeSession.sourceState="paused";
        fakeSession.lastPlaybackAtMs=now-72*3600000;
        compare(viewData.carouselSlides[0].key,"resume","the 72 hour boundary is inclusive");
        compare(viewData.carouselSlides[0].title,"Recent track");
        fakeSession.lastPlaybackAtMs=now-72*3600000-1;
        compare(viewData.carouselSlides[0].key,"spotlight-1","a stale playback timestamp is excluded");
        fakeSession.lastPlaybackAtMs=0;
        compare(viewData.carouselSlides[0].key,"spotlight-1","an absent timestamp cannot create a resume slide");
        fakeSession.lastPlaybackAtMs=now;
        fakeSession.sourceState="ended";
        compare(viewData.carouselSlides[0].key,"spotlight-1","ended playback is not resumable");
    }
}

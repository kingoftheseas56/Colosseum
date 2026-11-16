import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylHomeLoading"
    when:windowShown
    visible:true
    width:1280; height:700
    property var view:null
    property var gateData:null
    Rectangle { id:wallpaper; anchors.fill:parent; color:"#101015" }
    Component { id:shellFactory; Vinyl.VinylHouseShell { width:test.width; height:test.height; backdrop:wallpaper;contentBottomInset:80 } }
    Component {
        id:dataFactory
        QtObject {
            property var session:null
            property var personalLibrary:null
            property var recommendationService:null
            property bool usesRecommendationService:false
            property var heroSlides:[]
            property var crateItems:[]
            property var spinSongs:[]
            property var spinAlbums:[]
            property var spinArtists:[]
            property var v1HomeRows:[]
            property var libraryRows:[]
            property var picks:[{id:"one"},{id:"two"},{id:"three"},{id:"four"},{id:"five"}]
            property var homeRows:[]
            property var coldShelves:[]
            property var shelves:[]
            property var carouselSlides:[]
            property string activeTab:"home"
            property string lensTitle:"Everything"
            property var lens:({kind:"albums",genre:"",decade:"",country:"",language:""})
            property bool catalogueReachable:true
            property bool canEnterVinyl:true
            property int minimumArtistCount:5
            property bool tastePickerOpen:false
            property bool tasteGateLocked:false
            property bool pickerAnswered:true
            property string onboardingState:"answered"
            property string pickerQuery:""
            property var pickerDraft:[]
            property var savedPicks:[]
            property string artistSearchError:""
            property bool artistSearchLoading:false
            property bool artistSearchHasMore:false
            property bool discoveryLanding:false
            property bool onlineMode:false
            property bool discoveryLoading:false
            property string discoveryError:""
            property bool discoveryCached:false
            property string discoveryFetchedAt:""
            property bool cataloguePagingSupported:true
            property bool searchHasMore:false
            property bool searchLoading:false
            property string searchError:""
            property string homeStatus:"ready"
            property var homeSourcePool:({pendingCount:0})
            property string homeError:""
            property var homeListSnapshot:null
            signal listRequested(string key,bool home)
            signal artistSearchFocusRequested()
            function selectTab(tab){activeTab=tab;}
            function artwork(value){return value;}
            function artistOptions(query){return [];}
            function filterOptions(key){return [];}
            function setKind(kind){lens=Object.assign({},lens,{kind:kind});}
            function row(key,home){return {key:key,title:key,items:[]};}
            function listItems(key){return [];}
            function activate(item){}
            function playHomeItem(item){}
            function openHomeItem(item){}
            function saveHomeItem(item){}
            function radioHomeItem(item){}
            function hideHomeItem(item){}
            function pinCrateItem(item,pinned){}
            function feedbackHomeItem(item,action){}
            function beginTasteEdit(){tastePickerOpen=true;}
            function togglePickerArtist(item){}
            function cancelTasteEdit(){tastePickerOpen=false;}
            function skipPicker(){}
            function answerPicker(){}
            function retryArtistSearch(){}
            function loadMoreArtistSearch(){}
            function openList(key,home){listRequested(key,home);}
        }
    }
    function initTestCase(){
        // Start before Home has ever presented content, with the picker owning the screen.
        gateData=dataFactory.createObject(test,{homeStatus:"loading",tastePickerOpen:true});
        view=shellFactory.createObject(test,{worldData:gateData,runtimeReady:true,hostReady:true});
        verify(view!==null); verify(waitForRendering(view));
    }
    function cleanupTestCase(){view.destroy();gateData.destroy();wait(0);}
    function test_00_homeLoadingFollowsPendingWork() {
        var loading=findChild(view,"vinylHomeLoading"),bar=findChild(view,"vinylHomeLoadingBar");
        verify(loading!==null); verify(bar!==null); verify(!loading.visible);
        gateData.tastePickerOpen=true; gateData.homeStatus="loading";
        verify(!loading.visible,"The artist picker owns the screen until Save");
        gateData.tastePickerOpen=false; tryCompare(loading,"visible",true);
        verify(bar.indeterminate,"Provider work has no honest percentage");
        verify(!findChild(view,"vinylCrate").visible);
        verify(!findChild(view,"vinylSpin").visible);
        verify(!findChild(view,"vinylFlowTabs").visible);
        verify(!findChild(view,"vinylHome").visible,"An empty Home must not appear underneath the loading view");
        gateData.crateItems=[{id:"ready-album",kind:"albums",title:"Ready album",artist:"Artist",cover:""}];
        gateData.homeSourcePool={pendingCount:3}; gateData.homeStatus="ready";
        verify(loading.visible,"Optional Home rows must finish too");
        gateData.homeSourcePool={pendingCount:0}; tryCompare(loading,"visible",false);
        verify(findChild(view,"vinylCrate").visible);
        verify(findChild(view,"vinylSpin").visible);
        gateData.crateItems=[]; gateData.homeStatus="error";
        verify(loading.visible,"A failed catalogue needs a dedicated failure view");
        verify(!bar.indeterminate && !bar.visible,"A failed provider must not leave a permanent loading bar");
        compare(findChild(view,"vinylHomeLoadingMessage").text,"Could not load your music.");
        gateData.homeStatus="loading"; view.setTab("library"); verify(!loading.visible);
        view.setTab("home"); verify(loading.visible);
        gateData.crateItems=[{id:"ready-album",kind:"albums",title:"Ready album",artist:"Artist",cover:""}]; gateData.homeStatus="ready";
    }
    function test_01_refreshKeepsContent() {
        var loading=findChild(view,"vinylHomeLoading");
        var album={id:"ready-album",kind:"albums",title:"Ready album",artist:"Artist",cover:""};
        gateData.homeStatus="ready"; gateData.homeSourcePool={pendingCount:0};
        gateData.crateItems=[album];
        gateData.heroSlides=[{kind:"topAlbum",item:album,sideItems:[]}];
        gateData.v1HomeRows=[{key:"v1:albums-for-you",homeKey:"albums-for-you",title:"For you",items:[album]}];
        verify(waitForRendering(view));
        verify(!loading.visible);
        view.pageFlickable.contentY=100;
        var scrollY=view.pageFlickable.contentY;
        verify(scrollY>0);
        function verifyContent() {
            verify(!loading.visible,"Refresh must not replace existing content with loading or failure");
            for(var name of ["vinylHero","vinylCrate","vinylSpin","vinylFlowTabs","vinylHome","vinylHomeRow_albums-for-you"])
                verify(findChild(view,name).visible,name+" must stay visible during refresh");
            compare(view.pageFlickable.contentY,scrollY,"Refresh must preserve Home scroll position");
        }
        gateData.homeStatus="loading";
        verifyContent();
        gateData.homeStatus="idle";
        verifyContent();
        gateData.homeStatus="ready"; gateData.homeSourcePool={pendingCount:2};
        verifyContent();
        gateData.homeSourcePool={pendingCount:0}; gateData.homeStatus="error";
        verifyContent();
        // Opening the picker is not a latch reset.
        gateData.tastePickerOpen=true; gateData.homeStatus="loading";
        gateData.tastePickerOpen=false;
        verifyContent();
        // Each source independently counts as content; empty rows do not.
        gateData.crateItems=[]; gateData.heroSlides=[];
        verify(!loading.visible,"A populated Home row keeps the content latch");
        gateData.v1HomeRows=[{key:"v1:albums-for-you",homeKey:"albums-for-you",title:"For you",items:[]}];
        gateData.homeStatus="loading";
        tryCompare(loading,"visible",true);
        gateData.crateItems=[album];
        verify(loading.visible,"After emptying, partial content must wait for first-load work again");
        gateData.homeStatus="ready"; gateData.homeSourcePool={pendingCount:0};
        tryCompare(loading,"visible",false);
        verify(waitForRendering(view));
        // A populated but still-loading replacement must not inherit the previous world's latch.
        var replacement=dataFactory.createObject(test,{homeStatus:"loading",crateItems:[album]});
        try {
            view.worldData=replacement;
            tryCompare(loading,"visible",true);
            replacement.homeStatus="ready";
            tryCompare(loading,"visible",false);
            replacement.homeStatus="loading";
            verify(!loading.visible,"The replacement world can establish its own latch");
        } finally {
            view.worldData=gateData;
            replacement.destroy();
        }
        gateData.heroSlides=[]; gateData.v1HomeRows=[];
        gateData.homeStatus="ready"; gateData.homeSourcePool={pendingCount:0};
        view.pageFlickable.contentY=0;
    }
}

import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylCatalogueBrowse"
    visible:true
    QtObject {
        id:source
        property bool reject:false
        property int serial:0
        property var calls:[]
        property var cancelled:[]
        signal completed(string lane,int id,var data,string error)
        signal profileLeaving()
        signal shuttingDown()
        function request(lane,operation,value,options){if(reject)return -1;var id=++serial;calls=calls.concat([{id:id,lane:lane,operation:operation,value:value,options:options}]);return id;}
        function cancel(lane){cancelled=cancelled.concat([lane]);}
    }
    Vinyl.VinylCatalogueBrowseData {id:browse;source:source}
    width:1280;height:800
    Item {id:testBackdrop;anchors.fill:parent}
    Flickable {id:flick;width:1280;height:700;contentWidth:width;contentHeight:2000}
    QtObject {
        id:world
        property var session:null
        property var activated:null
        function artwork(url){return url || "";}
        function activate(item){activated=item;}
    }
    Component {id:pageFactory;Vinyl.VinylCatalogueBrowsePage {width:1200;browseData:browse;worldData:world;backdrop:testBackdrop;pageFlick:flick}}
    function test_component_controls_and_card_detail(){
        browse.activeKind="songs";reply(last("songs"),[{id:"song",title:"A song",artist:"An artist",kind:"songs",albumId:"container",trackId:"track",trackIndex:0}],{records:[{id:"container",tracks:[{id:"track"}]}]});
        var pane=createTemporaryObject(pageFactory,test);verify(pane!==null);wait(0);
        verify(pane.implicitHeight>100);var card=findChild(pane,"vinylCatalogueCard_0");verify(card!==null);card.triggered();compare(world.activated.albumId,"container");compare(browse.page("songs").selectedId,"song");
        browse.update("songs",{currentIndex:99});pane.focusCurrent();compare(pane.currentIndex,0);
        compare(findChild(pane,"vinylCatalogueMore").visible,false);
        pane.width=600;wait(0);verify(pane.implicitHeight>100);
        pane.kind="artists";wait(0);pane.focusCurrent();
        var market=findChild(pane,"vinylCatalogueMarket");verify(market.visible);verify(market.nextItemInFocusChain(true).activeFocus);
        verify(!findChild(pane,"vinylCatalogueCategory").activeFocus);
    }
    Component {
        id: routeDataFactory
        QtObject {
            property var session: null
            property var carouselSlides: []
            property var activations: []
            property int resumeCalls: 0
            property var openedLists: []
            property var homeRows: []
            property var coldShelves: []
            property var shelves: []
            property var personalLibrary: null
            property string activeTab: "home"
            property string lensTitle: "Everything"
            property var lens: ({kind:"albums",genre:"",decade:"",country:"",language:""})
            property bool catalogueReachable: true
            property bool canEnterVinyl: true
            property bool tastePickerOpen: false
            property bool pickerAnswered: true
            property string onboardingState: "answered"
            property string pickerQuery: ""
            property var pickerDraft: []
            property var savedPicks: []
            property int beginTasteEditCalls: 0
            property string artistSearchError: ""
            property bool artistSearchLoading: false
            property bool artistSearchHasMore: false
            property bool discoveryLanding: false
            property bool onlineMode: false
            property bool discoveryLoading: false
            property string discoveryError: ""
            property bool discoveryCached: false
            property string discoveryFetchedAt: ""
            property bool cataloguePagingSupported: true
            property bool searchHasMore: false
            property bool searchLoading: false
            property string searchError: ""
            property bool homePublicDiscovery: false
            property bool homeSelectedOffersLoading: false
            property bool homeEmpty: false
            property string homeStatus: "ready"
            property string homeError: ""
            signal listRequested(string key, bool home)
            signal artistSearchFocusRequested()
            function artistOptions(query) { return []; }
            function selectTab(tab) { activeTab=tab; }
            function beginTasteEdit() {
                beginTasteEditCalls++;
                pickerDraft=savedPicks.slice();
                onboardingState="editing";
                tastePickerOpen=true;
            }
            function openList(key,home) {
                if(home && key==="taste-artists") {
                    beginTasteEdit();
                    artistSearchFocusRequested();
                    return true;
                }
                openedLists=openedLists.concat([key]);
                listRequested(key,home);
                return false;
            }
            function row(key,home) { return {key:key,title:key,items:[]}; }
            function listItems(key) { return []; }
            function artwork(cover) { return ""; }
            function activate(item) { activations=activations.concat([item]); }
            function resume() { resumeCalls++; }
            function openHomeDestination(key) {}
            function togglePickerArtist(artist) {}
            function cancelTasteEdit() {
                pickerDraft=savedPicks.slice();
                onboardingState="answered";
                tastePickerOpen=false;
            }
            function skipPicker() {}
            function answerPicker() {}
            function retryArtistSearch() {}
            function loadMoreArtistSearch() {}
            function setKind(kind) { lens=Object.assign({},lens,{kind:kind}); }
            function filterOptions(key) { return []; }
        }
    }
    Component {id:routeWorldFactory;Vinyl.VinylWorld {width:1280;worldData:routeData;catalogueBrowse:browse;backdrop:testBackdrop;pageFlick:flick}}
    property var routeData:null
    function test_six_tab_route_restoration_and_carousel_parking(){
        routeData=routeDataFactory.createObject(test);
        var view=createTemporaryObject(routeWorldFactory,flick.contentItem);verify(view!==null);
        routeData.carouselSlides=[{key:"library",title:"Library",kicker:"Library",artKind:"square"}];
        var destinations=["home","songs","albums","artists","discover","library"];
        for(var i=0;i<destinations.length;i++){
            var tab=destinations[i];view.setTab(tab);compare(view.currentTab,tab);compare(routeData.activeTab,tab);
            var route=view.captureRouteState();compare(route.currentTab,tab);
            view.setTab(destinations[(i+1)%destinations.length]);view.restoreRouteState(route);compare(view.currentTab,tab);
            compare(findChild(view,"vinylCarousel").visible,["songs","albums","artists"].indexOf(tab)<0);
        }
        routeData.canEnterVinyl=false;wait(0);compare(view.currentTab,"home");
        view.destroy();wait(0);routeData.destroy();wait(0);routeData=null;
    }
    function init(){browse.enabled=false;browse.active=false;browse.activeKind="home";browse.reset();source.calls=[];source.cancelled=[];source.reject=false;browse.enabled=true;browse.active=true;}
    function last(kind){return source.calls.filter(function(c){return c.value===kind;}).slice(-1)[0];}
    function reply(call,items,extra,error){source.completed(call.lane,call.id,Object.assign({version:1,kind:call.value,category:call.options.category,region:call.options.region,items:items,records:[],nextCursor:{},complete:true},extra || {}),error || "");}
    function test_rejected_and_malformed_requests_are_retryable(){
        source.reject=true;browse.activeKind="songs";compare(browse.categoriesLoading,false);verify(browse.categoriesError.length>0);compare(browse.page("songs").loading,false);verify(browse.page("songs").error.length>0);
        source.reject=false;browse.retry("songs");reply(last("songs"),[{id:"good"}]);compare(browse.page("songs").items.length,1);
        browse.fetch("songs",false,true);reply(last("songs"),{});verify(browse.page("songs").error.length>0);compare(browse.page("songs").items[0].id,"good");
        browse.requestCategories(true);var call=source.calls.slice(-1)[0];source.completed(call.lane,call.id,{categories:"bad"},"");verify(browse.categoriesError.length>0);compare(browse.categoriesLoading,false);
    }
    function test_unrelated_and_stale_replies_skip_materialization(){
        browse.activeKind="songs";var current=last("songs"),copies=0;
        var payload={toJSON:function(){copies++;return {items:[]};}};
        var before=JSON.stringify(browse.pages);
        source.completed("home",current.id,payload,"");
        source.completed("play",current.id,payload,"");
        source.completed("songs",current.id,payload,"");
        source.completed("catalogue-songs",current.id+100,payload,"");
        compare(copies,0);compare(JSON.stringify(browse.pages),before);
    }
    function test_lazy_no_query_and_categories_cached(){
        compare(source.calls.length,0);
        browse.activeKind="songs";compare(source.calls.length,2);compare(last("songs").operation,"browseCatalogue");compare(last("songs").options.category,"");
        var categories=source.calls[0];source.completed(categories.lane,categories.id,{categories:[{title:"Rock",params:"opaque-rock",group:"Genres"}]},"");
        reply(last("songs"),[{id:"s1"}]);browse.activeKind="albums";compare(source.calls.length,3);reply(last("albums"),[{id:"a1"}]);
        browse.activeKind="songs";compare(source.calls.length,3);compare(browse.page("songs").items[0].id,"s1");compare(browse.categories[0].params,"opaque-rock");
    }
    function test_independent_category_market_and_focus(){
        browse.activeKind="songs";browse.select("songs","rock","");reply(last("songs"),[{id:"s"}]);browse.update("songs",{currentIndex:3,selectedId:"s",scrollY:240});
        browse.activeKind="albums";browse.select("albums","jazz","");reply(last("albums"),[{id:"a"}]);
        browse.activeKind="artists";browse.select("artists","","IN");reply(last("artists"),[{id:"r"}],{markets:[{code:"ZZ",title:"Global"},{code:"IN",title:"India"}]});
        browse.activeKind="songs";compare(browse.page("songs").category,"rock");compare(browse.page("songs").selectedId,"s");compare(browse.page("songs").currentIndex,3);compare(browse.page("songs").scrollY,240);
        compare(browse.page("albums").category,"jazz");compare(browse.page("artists").region,"IN");compare(browse.markets[1].title,"India");
    }
    function test_stale_selection_profile_and_shutdown(){
        browse.activeKind="songs";var old=last("songs");browse.select("songs","rock","");var current=last("songs");reply(old,[{id:"old"}]);compare(browse.page("songs").items.length,0);reply(current,[{id:"new"}]);compare(browse.page("songs").items[0].id,"new");
        browse.fetch("songs",false,true);old=last("songs");browse.profileGeneration++;reply(old,[{id:"wrong-profile"}]);compare(browse.page("songs").items.length,0);
        current=last("songs");source.profileLeaving();reply(current,[{id:"leaving"}]);compare(browse.records.length,0);compare(browse.page("songs").items.length,0);
        browse.active=false;compare(browse.page("songs").loading,false);
    }
    function test_finite_continuation_dedup_and_retry_retains_content(){
        browse.activeKind="songs";reply(last("songs"),[{id:"one"}],{records:[{id:"record",tracks:[{id:"one"}]}]});
        compare(browse.page("songs").complete,true);var count=source.calls.length;browse.fetch("songs",true,false);compare(source.calls.length,count);compare(browse.records[0].tracks[0].id,"one");
        browse.fetch("songs",false,true);reply(last("songs"),[{id:"one"}],{complete:false,nextCursor:{token:"next"}});
        browse.fetch("songs",true,false);reply(last("songs"),[],{},"Network failed");compare(browse.page("songs").items.length,1);compare(browse.page("songs").error,"Network failed");
        browse.retry("songs");compare(last("songs").options.cursor.token,"next");reply(last("songs"),[{id:"one"},{id:"two"}],{complete:false,nextCursor:{token:"last"}});compare(browse.page("songs").items.length,2);
        browse.fetch("songs",true,false);reply(last("songs"),[{id:"two"}],{complete:false,nextCursor:{token:"last"}});compare(browse.page("songs").complete,true);
    }
    function test_gate_reset_and_detail_pause(){
        browse.activeKind="albums";reply(last("albums"),[{id:"album"}],{records:[{id:"album"}]});browse.active=false;compare(browse.page("albums").items.length,1);compare(browse.records.length,1);
        browse.active=true;compare(source.calls.filter(function(c){return c.value==="albums";}).length,1);
        browse.enabled=false;compare(browse.records.length,0);compare(browse.page("albums").items.length,0);
    }
}

import QtQuick
import QtTest
TestCase {
    name: "VinylWorldNavigation"
    when: windowShown
    property var host
    property var gateData
    Component {
        id: gateDataFactory
        QtObject {
            property var session: null
            property var carouselSlides: []
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
                listRequested(key,home);
                return false;
            }
            function row(key,home) { return {key:key,title:key,items:[]}; }
            function listItems(key) { return []; }
            function artwork(cover) { return ""; }
            function activate(item) {}
            function resume() {}
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
    function item(name) { return findChild(host.contentItem,name) }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null)
        host.pageData.frozen=true
        host.page="world"
        tryCompare(host,"ready",true)
    }
    function cleanup() {
        if(host){host.close();host.destroy()}
        if(gateData){gateData.destroy();gateData=null}
    }
    function test_carouselSameAcrossTabs() {
        var world=item("vinylWorld"), carousel=item("vinylCarousel")
        verify(world!==null && carousel!==null)
        var playing=host.pageData.playingEditionId
        ;["home","discover","library"].forEach(function(tab) {
            world.setTab(tab); compare(item("vinylCarousel"),carousel)
            verify(carousel.visible);compare(host.pageData.playingEditionId,playing)
        })
        host.worldData.resume();compare(host.page,"song")
    }
    function test_primaryCtasUseIconGlyphsAndAccessibleLabels() {
        var carousel=item("vinylCarousel")
        verify(carousel!==null)
        tryVerify(function(){return carousel.slides.length>0
            && findChild(carousel,"featuredPrimaryIcon_0")!==null})
        carousel.slides.forEach(function(slide,index) {
            var expected=slide.key==="resume" ? "play" : "chevronRight"
            compare(slide.primaryIcon,expected)
            compare(slide.artKind,"square","Vinyl album and artist artwork keeps its square aspect")
            var icon=findChild(carousel,"featuredPrimaryIcon_"+index)
            var label=findChild(carousel,"featuredPrimaryLabel_"+index)
            var action=findChild(carousel,"featuredPrimaryAction_"+index)
            var art=findChild(carousel,"featuredInsetArt_"+index)
            verify(icon!==null && icon.visible,index+" renders its primary icon")
            compare(icon.kind,expected)
            verify(label!==null && !label.visible,index+" hides its visible CTA text")
            verify(art!==null && art.visible,index+" renders inset cover art")
            verify(Math.abs(art.width/art.height-1)<0.01,index+" keeps cover art square")
            verify(action!==null)
            compare(action.accessibleName,slide.cta,
                "the icon action keeps the supplied CTA as its accessible name")
            compare(action.accessibleDescription,"",
                "the icon action adds no hover descriptor")
        })
        var originalWidth=host.width
        host.width=520
        tryVerify(function(){return carousel.width<600})
        compare(findChild(carousel,"featuredBleedArt_0").fillMode,Image.PreserveAspectFit,
            "square music art remains uncropped on narrow cards")
        verify(!carousel.slides[0].art || String(findChild(carousel,"featuredBleedArt_0").source).length>0,
            "narrow music cards keep their artwork source")
        host.width=originalWidth
    }
    function test_snapshotAlbumRoundTrip() {
        var world=item("vinylWorld"),carousel=item("vinylCarousel")
        verify(world!==null)
        world.setTab("discover")
        verify(carousel.slides.length>1,"the route fixture must provide multiple featured slides")
        var featuredIndex=carousel.slides.length-1
        carousel.index=featuredIndex
        var routeState=world.captureRouteState()
        var chosen=host.worldData.listItems("popular")[2]
        var y=world.contentTop
        host.worldScroll.contentY=y
        host.worldData.activate(chosen)
        compare(host.page,"album")
        compare(host.pageData.shownAlbumId,chosen.albumId)
        compare(host.pageData.shownEdition.title,chosen.title)
        var a=host.worldData.catalogue.albums.find(function(a){return a.id===chosen.albumId})
        compare(host.pageData.tracks.length,a.tracks.length)
        mouseClick(item("vinylAlbumBack"))
        compare(host.page,"world")
        world.restoreRouteState(routeState)
        compare(carousel.index,featuredIndex,"the featured slide must survive detail and Back")
        compare(world.currentTab,"discover")
        verify(Math.abs(host.worldScroll.contentY-y)<4)
    }
    function test_seeAllReturnAndSongStays() {
        var world=item("vinylWorld");verify(world!==null)
        world.setTab("discover");host.worldData.openList("popular",false)
        compare(world.listKey,"popular")
        host.worldData.activate(host.worldData.listItems("popular")[1]);compare(host.page,"album")
        mouseClick(item("vinylAlbumBack"));compare(world.listKey,"popular")
        host.worldData.setKind("songs")
        var s=host.worldData.listItems("popular")[0]
        host.worldData.activate(s)
        compare(host.page,"world")
        compare(host.pageData.playingTrack.title,s.title)
    }
    function test_songAlbumRoundTripKeepsWorldOrigin() {
        var world=item("vinylWorld");world.setTab("discover")
        host.worldData.activate(host.worldData.listItems("popular")[2])
        host.pageData.playTrack(0)
        host.openSong()
        var song=item("vinylSongPage")
        verify(song!==null)
        song.albumRequested()
        compare(host.page,"album")
        mouseClick(item("vinylAlbumBack"))
        compare(host.page,"world")
        compare(world.currentTab,"discover")
    }
    function test_playingOriginalAlbumCanReplaceInspectedSnapshot() {
        var original=host.pageData.playingEditionId
        host.worldData.activate(host.worldData.listItems("popular")[2])
        verify(host.pageData.shownEditionId!==original)
        host.openSong()
        item("vinylSongPage").albumRequested()
        compare(host.pageData.shownEditionId,original)
        compare(host.pageData.shownEdition.name,host.pageData.playingEdition.name)
    }
    function test_repeatedSongRequestKeepsBackOrigin() {
        host.openSong();host.openSong()
        item("vinylSongPage").backRequested()
        compare(host.page,"world")
    }
    function test_browsingDoesNotCancelContinuousPlay() {
        host.worldData.setProfile("seasoned")
        var mix=host.worldData.homeRows.find(function(r){return r.kind==="mixes"}).items[0]
        host.worldData.activate(mix)
        var queue=host.pageData.continuousQueue.length
        verify(queue>0)
        var playing=host.pageData.playingEditionId
        host.worldData.activate(host.worldData.listItems("popular")[2])
        compare(host.pageData.continuousQueue.length,queue)
        compare(host.pageData.playingEditionId,playing)
        host.pageData.playTrack(0)
        compare(host.pageData.continuousQueue.length,0)
    }
    function test_leavingListKeepsShelfOffset() {
        var world=item("vinylWorld");world.setTab("discover")
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop+220
        var origin=host.worldScroll.contentY
        host.worldData.openList("popular",false)
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop+650
        waitForRendering(host.contentItem)
        var listOffset=host.worldScroll.contentY
        world.setTab("home")
        waitForRendering(host.contentItem)
        world.setTab("discover")
        waitForRendering(host.contentItem)
        compare(world.listKey,"popular")
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-listOffset)<4})
        world.backFromList()
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-origin)<4})
    }
    function test_snapshotMissingSectionsAbsent() {
        verify(host.worldData!==undefined)
        host.worldData.activate(host.worldData.listItems("popular")[0])
        ;["about","credits","videos","editions","information"].forEach(function(k) {
            verify(!item("vinylSection_"+k).visible,k)
        })
        host.pageData.setFixture("thriller")
        ;["about","credits","videos","editions","information"].forEach(function(k) {
            verify(item("vinylSection_"+k).visible,k)
        })
    }
    function test_explicitGateKeepsWorldNavigationOnHome() {
        var world=item("vinylWorld")
        gateData=gateDataFactory.createObject(host.contentItem)
        gateData.carouselSlides=host.worldData.carouselSlides
        gateData.homeRows=host.worldData.homeRows
        gateData.shelves=host.worldData.shelves
        world.worldData=gateData
        world.setTab("discover")
        compare(world.currentTab,"discover","an absent gate is covered by the existing real-data tests")
        world.showList("sample",false)
        compare(world.listKey,"sample")
        gateData.canEnterVinyl=false
        tryCompare(world,"currentTab","home")
        compare(world.listKey,"")
        world.setTab("library")
        compare(world.currentTab,"home")
        world.showList("sample",true)
        compare(world.listKey,"")
        compare(gateData.activeTab,"home")
    }
    function test_homeYourArtistsSeeAllOpensAndFocusesTastePicker() {
        var world=item("vinylWorld")
        gateData=gateDataFactory.createObject(host.contentItem)
        gateData.savedPicks=["artist-one","artist-two","artist-three","artist-four","artist-five"]
        gateData.pickerDraft=gateData.savedPicks.slice()
        gateData.homeRows=[{
            key:"taste-artists",title:"Your artists",kind:"artists",
            items:[{id:"artist-one",kind:"artists",title:"Artist One",artist:"Artist One",cover:"",round:true}]
        }]
        world.worldData=gateData
        world.setTab("home")

        var seeAll=item("vinylHomeSeeAll_taste-artists")
        tryVerify(function(){return seeAll!==null && seeAll.visible && seeAll.enabled})
        verify(!item("vinylPickerArtistSearch").visible,"ordinary Home has no inline artist search")
        mouseClick(seeAll)

        compare(gateData.beginTasteEditCalls,1)
        tryCompare(gateData,"onboardingState","editing")
        tryVerify(function(){return gateData.tastePickerOpen && item("vinylTastePicker").visible})
        compare(world.listKey,"","Your artists does not open the generic browse list")
        compare(JSON.stringify(gateData.pickerDraft),JSON.stringify(gateData.savedPicks),
            "the editor begins with the saved picks intact")
        compare(item("vinylPickerDone").enabled,true,"the existing five artist minimum remains satisfied")
        tryCompare(item("vinylPickerArtistSearch"),"activeFocus",true)
        mouseClick(item("vinylPickerCancel"))
        tryVerify(function(){return !gateData.tastePickerOpen && !item("vinylTastePicker").visible})
        verify(!item("vinylPickerArtistSearch").visible,"Cancel returns to Home without an inline search field")
        compare(JSON.stringify(gateData.pickerDraft),JSON.stringify(gateData.savedPicks),
            "Cancel restores the saved artist choices")
    }
}

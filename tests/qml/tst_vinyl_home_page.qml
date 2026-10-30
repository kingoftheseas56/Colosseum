import QtQuick
import QtTest
TestCase {
    name: "VinylHomePage"
    when: windowShown
    property var host
    property var probeData
    property var probeHome
    property var probePicker
    Component {
        id: homeDataFactory
        QtObject {
            property var homeRows: []
            property var coldShelves: []
            property string activeTab: "home"
            property string homeStatus: "ready"
            property string homeError: ""
            property bool tastePickerOpen: false
            property bool pickerAnswered: true
            property string onboardingState: "answered"
            property string pickerQuery: ""
            property var pickerDraft: []
            property string artistSearchError: ""
            property bool artistSearchLoading: false
            property bool artistSearchHasMore: false
            property int artistSearchSubmitCalls: 0
            property int refreshCalls: 0
            property var activatedItem: null
            function artistOptions(query) { return []; }
            function setArtistSearchQuery(value) { pickerQuery=value; }
            function submitArtistSearch() { artistSearchSubmitCalls++; }
            function refreshHomeSelection() { refreshCalls++; }
            function activate(item) { activatedItem=item; }
            function artwork(cover) { return ""; }
            function togglePickerArtist(artist) {}
            function cancelTasteEdit() {}
            function answerPicker() {}
            function retryArtistSearch() {}
            function loadMoreArtistSearch() {}
            function openList(key,home) {}
            function openHomeDestination(key) {}
        }
    }
    function item(name) { return findChild(host.contentItem,name) }
    function typeLatinText(value) {
        for(var i=0;i<value.length;i++) {
            var code=value.charCodeAt(i)
            if(code>=97 && code<=122)code-=32
            keyClick(code,Qt.NoModifier)
        }
    }
    function uniqueOfferedArtists(picker) {
        var seen=Object.create(null),unique=[]
        picker.offered.forEach(function(artist) {
            var id=String(artist.key || artist.id || "")
            if(id && !seen[id]) {seen[id]=true;unique.push(artist)}
        })
        return unique
    }
    function selectFiveArtists(picker) {
        var artists=uniqueOfferedArtists(picker)
        verify(artists.length>=5,"the picker must offer at least five distinct artists")
        var ids=artists.slice(0,5).map(function(artist){return artist.key||artist.id||""})
        artists.slice(0,5).forEach(function(artist){picker.toggleArtist(artist)})
        compare(picker.selected.length,5,"five distinct artists are required; offered="+
            ids.join(",")+" selected="+JSON.stringify(picker.selected))
        var done=item("vinylPickerDone")
        verify(done!==null && done.enabled,"Done must enable at five selected artists")
        return done
    }
    function createHomeProbe(rows) {
        probeData=homeDataFactory.createObject(host.contentItem)
        probeData.homeRows=rows
        var c=Qt.createComponent("../../qml/vinyl/VinylHomePage.qml")
        verify(c.status===Component.Ready,c.errorString())
        probeHome=c.createObject(host.contentItem,{
            worldData:probeData,backdrop:host.contentItem,pageFlick:host.worldScroll,width:host.width
        })
        verify(probeHome!==null)
        probeHome.height=probeHome.implicitHeight
        return {data:probeData,home:probeHome}
    }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null); host.pageData.frozen=true; host.page="world"
        tryCompare(host,"ready",true)
        host.worldData.resetPicker()
    }
    function cleanup() {
        if(probePicker){probePicker.destroy();probePicker=null}
        if(probeHome){probeHome.destroy();probeHome=null}
        if(probeData){probeData.destroy();probeData=null}
        if(host){host.close();host.destroy()}
    }

    function test_pickerVisibleWithoutScrolling() {
        verify(host.worldData!==undefined)
        var world=item("vinylWorld")
        var fold=host.worldScroll.y+host.worldScroll.height
        function checkAll() {
            ;["vinylPickerQuestion","vinylPickerDone"].forEach(function(name){
                var it=item(name)
                verify(it!==null,name+" must exist")
                var p=it.mapToItem(host.contentItem,0,0)
                verify(p.y>=host.worldScroll.y-1 && p.y+it.height<=fold+1,name+" must lie fully inside the visible page")
            })
            verify(item("vinylPickerSkip")===null,"artist onboarding has no Skip action")
            verify(!item("vinylPickerDone").enabled,"Done stays disabled until five artists are selected")
        }
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-world.pickerParkY)<1})
        verify(item("vinylTastePicker").visible)
        checkAll()
        host.worldData.resetPicker()
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-world.pickerParkY)<1})
        checkAll()
    }
    function test_fiveArtistDoneRemainsReachableWhileArtistsScroll() {
        verify(host.worldData!==undefined)
        var fold=host.worldScroll.y+host.worldScroll.height
        var picker=item("vinylTastePicker")
        var scroll=item("vinylPickerScroll")
        verify(scroll!==null)
        tryVerify(function(){return scroll.contentHeight>scroll.height+200})
        var done=selectFiveArtists(picker)
        scroll.contentY=scroll.contentHeight-scroll.height
        waitForRendering(host.contentItem)
        verify(Math.abs(scroll.contentY-(scroll.contentHeight-scroll.height))<1,"artists must actually be scrolled")
        var p=done.mapToItem(host.contentItem,0,0)
        verify(p.y>=host.worldScroll.y-1 && p.y+done.height<=fold+1,
            "Done must stay fully inside the visible page while artists are scrolled")
        mouseClick(done)
        compare(host.worldData.picks.length,5,"Done saves the five selected artists")
    }
    function test_fiveArtistOnboardingAndSearch() {
        verify(host.worldData!==undefined)
        var picker=item("vinylTastePicker")
        verify(picker.visible)
        verify(item("vinylPickerSkip")===null)
        verify(!item("vinylPickerDone").enabled)
        host.worldScroll.contentY=item("vinylWorld").contentTop
        item("vinylWorld").setTab("discover");item("vinylWorld").setTab("home")
        verify(picker.visible,"Home returns to the open artist picker")
        var search=item("vinylPickerArtistSearch")
        verify(search!==null,"the artist picker has its own search field")
        compare(search.placeholderText,"Search artists")
        compare(search.Accessible.name,"Search artists")
        verify(!search.activeFocus,"opening the picker does not move focus into search")
        verify(picker.focusArtistSearch(),"the parent's explicit search action can focus this field")
        tryCompare(search,"activeFocus",true)
        typeLatinText("rahman")
        compare(host.worldData.pickerQuery,"rahman","typed picker search uses the existing artist query API")
        verify(picker.offered.length>0)
        picker.offered.forEach(function(i){verify(i.title.toLowerCase().indexOf("rahman")>=0)})
        keyClick(Qt.Key_Return)
        compare(host.worldData.pickerQuery,"rahman")
        search.text=""
        host.worldData.setPickerQuery("")
        var status=item("vinylPickerStatus")
        compare(status.text,"0 of 5 artists selected.","the picker shows progress below its five artist minimum")
        var done=selectFiveArtists(picker)
        compare(status.text,"5 artists selected.")
        var extra=uniqueOfferedArtists(picker).find(function(artist) {
            return picker.selected.indexOf(String(artist.key || artist.id || ""))<0
        })
        verify(extra!==undefined,"the fixture offers a sixth distinct artist")
        picker.toggleArtist(extra)
        compare(picker.selected.length,6)
        compare(status.text,"6 artists selected.","the picker does not imply five is a selection cap")
        verify(done.enabled,"Done stays enabled above the five artist minimum")
        picker.toggleArtist(extra)
        compare(picker.selected.length,5)
        mouseClick(done)
        compare(host.worldData.picks.length,5,"Done saves five artists from the current draft")
        verify(!item("vinylTasteEdit").visible,
            "The prototype must not expose Edit tastes without an edit API")
        verify(host.worldData.homeRows.length>0)
        compare(host.worldData.coldShelves.length,0)
    }
    function test_pickerEnterSubmitsArtistSearch() {
        probeData=homeDataFactory.createObject(host.contentItem)
        probeData.onboardingState="editing"
        var component=Qt.createComponent("../../qml/vinyl/VinylTastePicker.qml")
        verify(component.status===Component.Ready,component.errorString())
        probePicker=component.createObject(host.contentItem,{
            worldData:probeData,backdrop:host.contentItem,pageFlick:host.worldScroll,
            width:host.width,height:host.worldScroll.height
        })
        verify(probePicker!==null)
        var search=findChild(probePicker,"vinylPickerArtistSearch")
        verify(search!==null)
        search.forceActiveFocus()
        tryCompare(search,"activeFocus",true)
        typeLatinText("radiohead")
        compare(probeData.pickerQuery,"radiohead")
        keyClick(Qt.Key_Return)
        compare(probeData.artistSearchSubmitCalls,1,"Enter submits through worldData.submitArtistSearch")
    }
    function test_pickerDraftDoesNotCrossProfiles() {
        host.worldData.setProfile("seasoned")
        host.worldData.resetPicker()
        var picker=item("vinylTastePicker")
        selectFiveArtists(picker)
        compare(picker.selected.length,5)
        host.worldData.setProfile("new")
        compare(picker.selected.length,0)
        compare(host.worldData.picks.length,0,"draft selections do not cross profiles")
    }
    function test_doneRequiresFiveAndDiscoverRemainsAccessible() {
        verify(host.worldData!==undefined)
        var world=item("vinylWorld");verify(item("vinylTastePicker").visible)
        var done=item("vinylPickerDone")
        verify(done!==null && !done.enabled,"Done is gated below five artists")
        world.setTab("discover");compare(world.currentTab,"discover")
        world.setTab("home");host.worldScroll.contentY=world.contentTop
        selectFiveArtists(item("vinylTastePicker"))
        mouseClick(done)
        compare(host.worldData.picks.length,5)
        verify(host.worldData.pickerAnswered,"selecting five artists completes onboarding")
    }
    function test_seasonedRowsAndOffline() {
        verify(host.worldData!==undefined)
        host.worldData.setProfile("seasoned")
        var page=item("vinylHomePage")
        compare(page.rowKeys[0],"jump-back")
        compare(page.rowKeys[page.rowKeys.length-1],"rediscovery")
        compare(page.rowKeys.filter(function(k){return k.indexOf("recommend-")===0}).length,5)
        ;["popular","trending","new","essentials","global"].forEach(function(k){verify(page.rowKeys.indexOf(k)<0)})
        var rows=JSON.stringify(page.rowKeys)
        host.worldData.setCatalogueReachable(false);compare(JSON.stringify(page.rowKeys),rows)
        host.worldData.openList(page.rowKeys[1],true)
        compare(item("vinylListPage").itemCount,host.worldData.row(page.rowKeys[1],true).items.length)
    }
    function test_dataFedHomeRowsRenderAndActivateGenerically() {
        var mix={id:"mix-a",kind:"mixes",title:"Supplied mix",subtitle:"Made for you",cover:""}
        var radio={id:"radio-a",kind:"radio",title:"Artist radio",subtitle:"Based on your artists",cover:""}
        var rows=[
            {key:"quick-picks",title:"Quick Picks",kind:"songs",items:[{id:"song-a",kind:"songs",title:"Supplied song",subtitle:"Artist",cover:""}]},
            {key:"made-for-you",title:"Made for you",kind:"mixes",items:[mix,{id:"mix-b",kind:"mixes",title:"Second mix",cover:""},radio]},
            {key:"from-your-artists",title:"From your artists",kind:"artists",items:[{id:"artist-a",kind:"artists",title:"Supplied artist",cover:"",round:true}]},
            {key:"adjacent-albums",title:"Albums near your artists",kind:"albums",items:[{id:"album-a",kind:"albums",title:"Adjacent album",cover:""}]},
            {key:"adjacent-artists",title:"Artists near your artists",kind:"artists",items:[{id:"artist-b",kind:"artists",title:"Adjacent artist",cover:"",round:true}]},
            {key:"new-releases",title:"New releases",kind:"albums",items:[{id:"album-b",kind:"albums",title:"New release",cover:""}]},
            {key:"rediscovery",title:"Rediscovery",kind:"songs",items:[{id:"song-b",kind:"songs",title:"Rediscovered song",cover:""}]},
            {key:"less-played",title:"Less played",kind:"songs",items:[{id:"song-c",kind:"songs",title:"Less played song",cover:""}]},
            {key:"regional-charts",title:"Regional charts",kind:"songs",items:[{id:"region-a",kind:"songs",title:"Regional item",cover:""}]},
            {key:"trending",title:"Trending",kind:"songs",items:[{id:"trend-a",kind:"songs",title:"Trending item",cover:""}]},
            {key:"saved-playlists",title:"Saved playlists",kind:"playlists",items:[{id:"saved-a",kind:"mixes",title:"Saved playlist",cover:""}]},
            {key:"owned-playlists",title:"Owned playlists",kind:"playlists",items:[{id:"owned-a",kind:"mixes",title:"Owned playlist",cover:""}]}
        ]
        var probe=createHomeProbe(rows),page=probe.home
        var expected=["quick-picks","made-for-you","from-your-artists","adjacent-albums",
            "adjacent-artists","new-releases","rediscovery","less-played"]
        compare(JSON.stringify(page.rowKeys),JSON.stringify(expected))
        expected.forEach(function(key) {
            var shelf=findChild(page,"vinylHomeRow_"+key)
            verify(shelf!==null,key+" must render as a Home shelf")
            verify(shelf.hasCards,key+" must render its supplied cards")
            verify(findChild(page,"vinylCard_"+key+"_0")!==null,key+" must use the standard card component")
        })
        verify(findChild(page,"vinylHomeRow_regional-charts")===null)
        verify(findChild(page,"vinylHomeRow_trending")===null)
        verify(findChild(page,"vinylHomeRow_saved-playlists")===null)
        verify(findChild(page,"vinylHomeRow_owned-playlists")===null)
        var refresh=findChild(page,"vinylHomeRefresh")
        verify(refresh!==null && refresh.visible && refresh.enabled)
        compare(refresh.accessibleName,"Refresh Home")
        mouseClick(refresh)
        compare(probe.data.refreshCalls,1)
        var mixCard=findChild(page,"vinylCard_made-for-you_0")
        verify(mixCard!==null)
        mouseClick(mixCard)
        compare(probe.data.activatedItem.id,"mix-a",
            "mixes and radio cards must use the same worldData.activate path")
    }
}

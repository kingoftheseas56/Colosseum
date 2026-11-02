import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl
TestCase {
    id: test
    name: "VinylCarouselActions"
    visible: true
    when: windowShown
    width: 1280; height: 850
    property var sourceData: null
    property var world: null
    Component {
        id: sourceFactory
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
    Flickable { id: viewport; anchors.fill: parent; contentHeight: world ? world.implicitHeight : height }
    Component { id: worldFactory; Vinyl.VinylWorld {
        width: viewport.width; height: implicitHeight
        worldData: test.sourceData; backdrop: viewport; pageFlick: viewport
    } }
    function init() {
        sourceData=sourceFactory.createObject(test);
        world=createTemporaryObject(worldFactory,viewport.contentItem);
        verify(world!==null);
    }
    function cleanup() { if(world){world.destroy();wait(0);}world=null;sourceData.destroy();wait(0);sourceData=null; }
    function test_namedLabelsAndUnchangedIdentityDispatch() {
        var cases=[
            {key:"artist",item:{id:"artist-one",kind:"artists",destination:{kind:"artist",providerId:"provider-artist"}},label:"Explore artist"},
            {key:"album",item:{id:"album-one",kind:"albums",destination:{kind:"album",providerId:"provider-album"}},label:"Open album"},
            {key:"song",item:{id:"song-one",kind:"songs",trackIndex:2,occurrence:7,ref:{kind:"recording",itemId:"recording-one"}},label:"View song"},
            {key:"continue",item:{id:"checkpoint-one",kind:"resume",resumeEntry:{identity:"checkpoint"}},label:"Resume"},
            {key:"mix",item:{id:"mix-one",kind:"mixes",mixId:"personal"},label:"Open mix"},
            {key:"radio",item:{id:"radio-one",kind:"radio",seedArtist:{providerId:"seed-one"}},label:"Start radio"},
            {key:"resume",label:"Resume"}, {key:"library",label:"Open library"},
            {key:"trending",label:"Explore collection"}
        ];
        var carousel=findChild(world,"vinylCarousel");verify(carousel!==null);
        for(var row of cases) {
            sourceData.carouselSlides=[Object.assign({title:"Current spotlight",kicker:"Spotlight",artKind:"square"},row)];
            wait(10);
            compare(carousel.slides[0].cta,row.label);compare(carousel.slides[0].primaryIcon,"");
            var label=findChild(carousel,"featuredPrimaryLabel_0");
            var button=findChild(carousel,"featuredPrimaryAction_0");
            verify(label!==null && label.visible);compare(label.text,row.label);
            compare(button.accessibleName,row.label);
            var before=sourceData.activations.length;
            verify(button.activate(Qt.MouseFocusReason));
            if(row.item) { compare(sourceData.activations.length,before+1);compare(sourceData.activations[before],row.item); }
        }
        sourceData.carouselSlides=[Object.assign({title:"Artist",kicker:"Spotlight"},cases[0]),
            Object.assign({title:"Song",kicker:"Spotlight"},cases[2])];
        carousel.index=0;wait(20);
        var view=findChild(carousel,"featuredCarouselView");verify(view!==null);
        view.forceActiveFocus(Qt.TabFocusReason);keyClick(Qt.Key_Right);
        tryCompare(carousel,"index",1);
        var selected=findChild(carousel,"featuredPrimaryAction_1");
        verify(selected.activate(Qt.MouseFocusReason));
        compare(sourceData.activations[sourceData.activations.length-1],cases[2].item);
        compare(carousel.index,1,"Activating the selected slide must preserve manual navigation");
        compare(sourceData.resumeCalls,1);
        verify(sourceData.openedLists.indexOf("trending")>=0);
    }
}

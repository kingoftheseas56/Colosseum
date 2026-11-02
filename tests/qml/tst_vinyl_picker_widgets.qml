import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylPickerWidgets"
    when: windowShown
    visible: true
    width: 960; height: 720
    property var picker: null
    QtObject {
        id: worldStub
        property string pickerQuery: ""
        property var pickerDraft: []
        property int minimumArtistCount: 5
        property var recommendationService: null
        property bool usesRecommendationService: false
        property string onboardingState: "unanswered"
        property string artistSearchError: ""
        property bool artistSearchLoading: false
        property bool artistSearchHasMore: false
        property var toggles: []
        property int answers: 0
        property int cancels: 0
        property var initial: [
            {key:"one",title:"Initial One",cover:"",artistRef:{provider:"youtube-music",providerId:"one"}},
            {key:"two",title:"Initial Two",cover:"",artistRef:{provider:"youtube-music",providerId:"two"}},
            {key:"three",title:"Initial Three",cover:""},
            {key:"four",title:"Initial Four",cover:""},
            {key:"five",title:"Initial Five",cover:""}]
        property var searched: [{key:"search-one",title:"Search One",cover:"",
            artistRef:{provider:"youtube-music",providerId:"search-one"}},
            {key:"search-two",title:"Search Two",cover:""}]
        property var changed: [{key:"changed",title:"Changed Artist",cover:"",
            artistRef:{provider:"youtube-music",providerId:"changed"}}]
        function artwork(value) { return value || ""; }
        function artistOptions(query) { return query === "search" ? searched : query ? changed : initial; }
        function setArtistSearchQuery(query) { pickerQuery=query; }
        function togglePickerArtist(artist) {
            toggles=toggles.concat([artist]);
            pickerDraft=pickerDraft.indexOf(artist.key) < 0 ? pickerDraft.concat([artist.key])
                : pickerDraft.filter(function(key) { return key !== artist.key; });
        }
        function answerPicker() { answers++; }
        function cancelTasteEdit() { cancels++; }
    }
    Flickable { id: page; anchors.fill: parent; contentHeight: height }
    Component { id: factory; Vinyl.VinylTastePicker {
        width: page.width; height: page.height; pageFlick: page; backdrop: page; worldData: worldStub
    } }
    function typeText(value) { for(var i=0;i<value.length;++i) keyClick(value.charAt(i)); }
    function labelCount(node, value) {
        var count=node.text === value ? 1 : 0;
        for (var i=0; node.children && i<node.children.length; ++i) count+=labelCount(node.children[i],value);
        return count;
    }
    function init() {
        worldStub.pickerQuery=""; worldStub.pickerDraft=[]; worldStub.toggles=[]; worldStub.answers=0; worldStub.cancels=0;
        worldStub.onboardingState="unanswered";
        picker=createTemporaryObject(factory,page.contentItem);
        verify(picker !== null); wait(30);
    }
    function test_initialAndSearchedWidgetPointerAndKeyboard() {
        var first=findChild(picker,"vinylPickerArtist_0");
        var artwork=findChild(picker,"vinylPickerArtistArtwork_0");
        verify(artwork !== null);
        verify(artwork.includesTitle);
        compare(artwork.title,"Initial One");
        compare(labelCount(first,"Initial One"),1,"Only the record frame prints the artist name");
        verify(!first.showFocusFrame);
        compare(first.focusRadius,first.width*0.1);
        mouseClick(first,first.width/2,first.height/2);
        compare(worldStub.toggles.length,1); compare(worldStub.toggles[0],worldStub.initial[0]);
        verify(first.Accessible.checked);
        first.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return);
        compare(worldStub.toggles.length,2); compare(worldStub.toggles[1].artistRef,worldStub.initial[0].artistRef);
        verify(!first.Accessible.checked);
        var search=findChild(picker,"vinylPickerArtistSearch");
        search.forceActiveFocus(); typeText("search");
        tryCompare(worldStub,"pickerQuery","search"); wait(30);
        artwork=findChild(picker,"vinylPickerArtistArtwork_0");
        compare(artwork.title,"Search One");
        first=findChild(picker,"vinylPickerArtist_0");
        compare(labelCount(first,"Search One"),1);
        mouseClick(first,first.width/2,first.height/2);
        compare(worldStub.toggles[2],worldStub.searched[0]); verify(first.Accessible.checked);
        first.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return);
        compare(worldStub.toggles[3].artistRef,worldStub.searched[0].artistRef); verify(!first.Accessible.checked);
        search.forceActiveFocus(); keyClick(Qt.Key_A,Qt.ControlModifier); typeText("changed");
        tryCompare(worldStub,"pickerQuery","changed"); wait(30);
        compare(findChild(picker,"vinylPickerArtistArtwork_0").title,"Changed Artist");
        first=findChild(picker,"vinylPickerArtist_0"); mouseClick(first,first.width/2,first.height/2);
        compare(worldStub.toggles[4],worldStub.changed[0]);
    }
    function test_minimumAndEditingActions() {
        var done=findChild(picker,"vinylPickerDone");
        verify(!done.enabled);
        worldStub.initial.forEach(function(artist) { worldStub.togglePickerArtist(artist); });
        tryCompare(done,"enabled",true);
        done.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return); compare(worldStub.answers,1);
        worldStub.onboardingState="editing"; wait(30);
        var cancel=findChild(picker,"vinylPickerCancel"); verify(cancel.visible);
        mouseClick(cancel,cancel.width/2,cancel.height/2); compare(worldStub.cancels,1);
    }
}

import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl
import "../../qml/vinyl/VinylRecommendationActions.js" as Actions

TestCase {
    id: test
    name: "VinylRecommendationPage"
    visible: true
    width: 900; height: 700
    when: windowShown
    property var calls: []
    readonly property var itemRef: ({identity:"provider",kind:"recording",providerId:"youtube-music",itemId:"song-one"})
    readonly property var artistRef: ({identity:"provider",kind:"artist",providerId:"youtube-music",itemId:"artist-one"})
    QtObject {
        id: feedbackData
        property var session: null
        property var suppressed: []
        function artwork(cover) { return cover || ""; }
        function recommendationArtistRef(artist) { return artist.ref || null; }
        function suppressedRecommendationsForArtist(ref) { return suppressed; }
        function recommendationRefsEqual(left,right) { return JSON.stringify(left)===JSON.stringify(right); }
    }
    Component {
        id: pageFactory
        Vinyl.VinylRecommendationPage {
            width: 850; height: 600
            worldData: feedbackData; backdrop: null
            onActionRequested: (ref,action,generation) => test.calls=test.calls.concat([{ref:ref,action:action,generation:generation}])
        }
    }
    function cleanup() { calls=[]; feedbackData.suppressed=[]; }
    function request(artist) {
        var item={id:"song",title:"Song title",kind:artist ? "artists" : "songs",artist:"Artist name",
            ref:artist ? artistRef : itemRef,artistRefs:[{name:"Artist name",ref:artistRef}]};
        return {item:item,options:Actions.options(feedbackData,item,{key:artist ? "taste-artists" : "quick-picks"}),generation:7};
    }
    function test_directActionsPreserveTypedRefsAndGeneration() {
        var page=createTemporaryObject(pageFactory,test,{request:request(false)});
        verify(page !== null);
        var names=["like","dislike","neutral","hide","unhide","snooze-7d","unsnooze","exclude-artist-0","allow-artist-0"];
        names.forEach(function(key) {
            var control=page.actionFor(key);
            verify(control !== null,"Direct action exists: "+key);
            verify(control.symbol.length>0 && control.label.length>0);
            verify(!control.contextEnabled);
            verify(control.activate(Qt.ShortcutFocusReason));
        });
        compare(calls.length,9);
        compare(calls[0].ref,itemRef);
        compare(calls[7].ref,artistRef);
        verify(calls.every(function(call) { return call.generation===7; }));
    }
    function test_suppressedItemsRemainRecoverableFromArtist() {
        feedbackData.suppressed=[{title:"Hidden track",ref:itemRef,hidden:true,snoozed:true,excluded:true,
            excludedArtistRefs:[{ref:artistRef,name:"Artist name"}]}];
        var page=createTemporaryObject(pageFactory,test,{request:request(true)});
        ["exclude-current-artist","allow-current-artist","unhide-suppressed-0","unsnooze-suppressed-0","allow-excluded-artist-0-0"]
            .forEach(function(key) { verify(page.actionFor(key)!==null,key); });
        verify(page.actionFor("unhide-suppressed-0").activate(Qt.MouseFocusReason));
        compare(calls[0].action,"unhide");
        compare(calls[0].ref,itemRef);
        compare(page.groups.length,3);
    }
    function test_smallWindowAndLongRecoveryListScrollToKeyboardAction() {
        var options=[];
        for(var i=0;i<40;i++) options.push({key:"restore-"+i,action:"unhide",label:"Restore "+i,
            ref:{itemId:"hidden-"+i},section:"hidden",subject:"Hidden track "+i});
        var page=createTemporaryObject(pageFactory,test,{width:300,height:260,
            request:{item:{id:"artist",kind:"artists",title:"Artist",ref:artistRef},options:options,generation:7}});
        verify(page.contentHeight>page.height);
        var last=page.actionFor("restore-39");
        verify(last!==null);
        last.forceActiveFocus(Qt.TabFocusReason);
        wait(20);
        verify(page.contentY>0);
        var position=last.mapToItem(page.contentItem,0,0);
        verify(position.y>=page.contentY && position.y+last.height<=page.contentY+page.height);
        verify(position.x>=0 && position.x+last.width<=page.width);
        keyClick(Qt.Key_Return);
        compare(calls.length,1);
        compare(calls[0].ref.itemId,"hidden-39");
    }
}

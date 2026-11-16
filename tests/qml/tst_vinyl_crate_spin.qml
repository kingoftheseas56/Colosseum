import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id:test
    name:"VinylCrateSpin"
    when:windowShown
    visible:true
    width:1220; height:900
    property var crate:null
    property var spin:null
    Rectangle { id:wallpaper; anchors.fill:parent; color:"#11131a" }
    Flickable { id:page; anchors.fill:parent; contentWidth:width; contentHeight:2000; clip:true }
    Component { id:crateFactory; Vinyl.VinylCrate {} }
    Component { id:spinFactory; Vinyl.VinylSpin {} }
    SignalSpy { id:activated; target:test.crate || test.spin; signalName:"activateRequested" }
    SignalSpy { id:opened; target:test.crate || test.spin; signalName:"openRequested" }
    SignalSpy { id:pinned; target:test.crate; signalName:"pinRequested" }
    SignalSpy { id:feedback; target:test.spin; signalName:"feedbackRequested" }
    function item(kind,index) {
        return {kind:kind,id:kind+"-"+index,title:"Fixture "+index,artist:"Fixture artist",albumTitle:"Fixture album",
            reason:"Close to your city pop",cover:Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg")};
    }
    function mixed(count) {
        var rows=[];
        for(var i=0;i<count;++i) rows.push(item(["albums","artists","songs","mixes","radio"][i%5],i));
        rows[0]=Object.assign({},rows[0],{resumeTrack:4,totalTracks:12});
        if(count>4) rows[4]=Object.assign({},rows[4],{available:false,pinned:true});
        return rows;
    }
    function makeCrate(items) {
        crate=crateFactory.createObject(page.contentItem,{x:20,y:20,width:1172,compact:true,items:items,backdrop:wallpaper,pageFlick:page});
        verify(crate!==null); verify(waitForRendering(crate)); activated.clear(); opened.clear(); pinned.clear();
    }
    function makeSpin() {
        var songs=[],albums=[],artists=[];
        for(var i=0;i<13;++i) songs.push(item("songs",i));
        for(i=0;i<9;++i) { albums.push(item("albums",i)); artists.push(item("artists",i)); }
        spin=spinFactory.createObject(page.contentItem,{x:20,y:200,width:1172,songs:songs,albums:albums,artists:artists,backdrop:wallpaper,pageFlick:page});
        verify(spin!==null); verify(waitForRendering(spin)); activated.clear(); opened.clear(); feedback.clear();
    }
    function cleanup() {
        if(crate){crate.destroy();wait(0);crate=null;}
        if(spin){spin.destroy();wait(0);spin=null;}
        page.contentY=0; mouseMove(test,width-2,height-2);
    }
    function click(node,x,y) { verify(node!==null); verify(node.visible && node.enabled); mouseMove(node,x===undefined ? node.width/2 : x,y===undefined ? node.height/2 : y); verify(waitForRendering(node)); mouseClick(node,x,y); }
    function test_crateSixteenMixedSquareFacesInTwoRowsOfEight() {
        makeCrate(mixed(16)); compare(crate.objectName,"vinylCrate"); compare(crate.count,16);
        compare(crate.columns,8); compare(crate.pageSize,16); compare(crate.pageCount,1);
        var kinds=[];
        for(var i=0;i<16;++i){
            var tile=findChild(crate,"vinylCrateTile_"+i); verify(tile!==null); kinds.push(tile.kind);
            var face=findChild(tile,"vinylCrateTile_"+i+"_card_cover"); tryCompare(face,"status",Loader.Ready);
            compare(face.item.width,130.75); compare(face.item.height,130.75);
            var card=findChild(tile,"vinylCrateTile_"+i+"_card");
            verify(card.captionDetail.length>0);
            if(tile.kind==="songs") { compare(card.captionTitle,"Fixture "+i); compare(card.captionDetail,"Fixture artist · from Fixture album"); }
        }
        for(var kind of ["albums","artists","songs","mixes","radio"]) verify(kinds.indexOf(kind)>=0);
        var first=findChild(crate,"vinylCrateTile_0"), eighth=findChild(crate,"vinylCrateTile_7"), ninth=findChild(crate,"vinylCrateTile_8");
        compare(first.y,eighth.y); verify(ninth.y>first.y); compare(first.x,ninth.x);
        compare(findChild(crate,"vinylCrateTile_16"),null);
        compare(findChild(crate,"vinylCrateCaption"),null,"Hemanth removed the caption");
    }
    function test_crateResumeUsesExactOwnerTrackAndGoldCaption() {
        makeCrate(mixed(16));
        var tile=findChild(crate,"vinylCrateTile_0"); compare(tile.resumeTrack,4);
        var caption=findChild(tile,"vinylCrateTile_0_card_captionDetail");
        compare(caption.text,"Resume · track 4 of 12"); compare(String(caption.color),"#f0c44a");
        click(tile,20,100); compare(activated.count,1); compare(activated.signalArguments[0][0].resumeTrack,4);
        compare(activated.signalArguments[0][0].totalTracks,12); verify(activated.signalArguments[0][0].reason.indexOf("Resume")<0);
    }
    function test_cratePinSignalDoesNotMutateOwnerAndUnavailableCanUnpin() {
        var rows=mixed(16), before=JSON.stringify(rows);
        for(var row of rows) Object.freeze(row); Object.freeze(rows);
        makeCrate(rows);
        var tile=findChild(crate,"vinylCrateTile_0"), pin=findChild(crate,"vinylCrateTilePin_0");
        mouseMove(tile,60,80); tryCompare(pin,"revealed",true); click(pin);
        compare(pinned.count,1); compare(pinned.signalArguments[0][0].id,rows[0].id); compare(pinned.signalArguments[0][1],true);
        compare(tile.pinned,false); compare(JSON.stringify(rows),before);
        var missing=findChild(crate,"vinylCrateTile_4"), unpin=findChild(crate,"vinylCrateTilePin_4");
        verify(missing!==null && missing.visible); compare(missing.pinned,true); compare(missing.available,false);
        var card=findChild(missing,"vinylCrateTile_4_card"); verify(!card.enabled); verify(card.opacity<1);
        mouseClick(missing,20,100); compare(activated.count,0);
        verify(unpin.enabled,"Unavailable pinned tiles must allow unpin"); click(unpin); compare(pinned.count,2); compare(pinned.signalArguments[1][1],false);
        compare(missing.pinned,true); compare(JSON.stringify(rows),before); compare(activated.count,0);
    }
    function test_crateArtistOpenIsSeparateFromRadioActivation() {
        makeCrate(mixed(16));
        var tile=findChild(crate,"vinylCrateTile_1"); click(tile,20,100);
        compare(activated.count,1); compare(activated.signalArguments[0][0].kind,"artists"); compare(opened.count,0);
        var open=findChild(crate,"vinylCrateTileOpen_1"); mouseMove(tile,65,65); tryCompare(open,"revealed",true); click(open);
        compare(opened.count,1); compare(opened.signalArguments[0][0].id,activated.signalArguments[0][0].id); compare(activated.count,1);
    }
    function test_cratePagingAndNoTasteCard() {
        makeCrate(mixed(17)); compare(crate.pageCount,2); click(findChild(crate,"vinylCrateNext"));
        tryCompare(crate,"page",1); verify(findChild(crate,"vinylCrateTile_16")!==null); compare(findChild(crate,"vinylCrateTile_0"),null);
        click(findChild(crate,"vinylCratePrevious")); tryCompare(crate,"page",0);
        crate.items=[]; tryCompare(crate,"count",0);
        compare(findChild(crate,"vinylCrateTasteCard"),null); // the taste card is gone; the gate owns the picker
        crate.items=mixed(1); tryCompare(crate,"count",1);
        compare(findChild(crate,"vinylCrateTasteCard"),null);
    }
    function test_spinSongsTwelveCompactRowsAndPaging() {
        makeSpin(); compare(spin.view,"songs"); compare(spin.pageSize,12); compare(spin.pageCount,2);
        for(var i=0;i<12;++i) {var row=findChild(spin,"vinylSpinSong_"+i); verify(row!==null); compare(row.height,54);}
        var first=findChild(spin,"vinylSpinSong_0"), fourth=findChild(spin,"vinylSpinSong_3");
        var origin=first.mapToItem(spin,0,0), next=fourth.mapToItem(spin,0,0); compare(origin.x,next.x); verify(next.y>origin.y);
        compare(findChild(spin,"vinylSpinSong_12"),null);
        click(first,65,24); compare(activated.count,1); compare(activated.signalArguments[0][0].id,"songs-0");
        click(findChild(spin,"vinylSpinNext")); tryCompare(spin,"page",1); verify(findChild(spin,"vinylSpinSong_12")!==null);
        compare(findChild(spin,"vinylSpinSong_0"),null);
    }
    function test_spinSwitchKeepsFactsWithoutRecommendationCopyAndPreservesOuterScroll() {
        makeSpin(); page.contentY=120; var before=page.contentY;
        for(var view of ["albums","artists","songs"]){
            click(findChild(spin,"vinylSpinSwitch_"+view)); tryCompare(spin,"view",view);
            compare(findChild(spin,"vinylSpinSwitch").view,view); compare(page.contentY,before);
            if(view!=="songs") {
                var card=findChild(spin,"vinylSpinTile_0"); verify(card!==null); compare(card.presentationKind,view);
                compare(findChild(card,"vinylSpinTile_0_captionReason"),null);
                compare(card.captionDetail,view==="albums" ? "Fixture artist" : "Artist radio");
                verify(card.accessibleName.indexOf("Close to")<0);
                card.exposureRail=page;
                compare(card.captionDetail,view==="albums" ? "Fixture artist" : "Artist radio");
                compare(spin.pageSize,8); compare(spin.pageCount,2);
            }
        }
    }
    function test_spinOpenAndFeedbackAreTypedOwnerSignalsOnly() {
        makeSpin(); var before=JSON.stringify(spin.songs), row=findChild(spin,"vinylSpinSong_0");
        mouseMove(row,65,24); var open=findChild(spin,"vinylSpinOpen_0"); tryCompare(open,"revealed",true); click(open);
        compare(opened.count,1); compare(activated.count,0); compare(opened.signalArguments[0][0].id,"songs-0");
        click(findChild(spin,"vinylSpinLike_0")); click(findChild(spin,"vinylSpinHide_0"));
        compare(feedback.count,2); compare(feedback.signalArguments[0][1],"like"); compare(feedback.signalArguments[1][1],"hide");
        compare(feedback.signalArguments[0][0].id,"songs-0"); compare(JSON.stringify(spin.songs),before);
    }
    function test_spinAlbumArtistPagingAndSeparateOpenAction() {
        makeSpin();
        for(var view of ["albums","artists"]){
            click(findChild(spin,"vinylSpinSwitch_"+view)); tryCompare(spin,"view",view);
            click(findChild(spin,"vinylSpinNext")); tryCompare(spin,"page",1);
            verify(findChild(spin,"vinylSpinTile_8")!==null); compare(findChild(spin,"vinylSpinTile_0"),null);
            click(findChild(spin,"vinylSpinPrevious")); tryCompare(spin,"page",0);
            var card=findChild(spin,"vinylSpinTile_0"), prior=activated.count;
            click(card,20,100); compare(activated.count,prior+1); compare(activated.signalArguments[prior][0].kind,view);
            var open=findChild(spin,"vinylSpinOpen_0"), priorOpen=opened.count;
            mouseMove(card,65,65); tryCompare(open,"revealed",true); click(open);
            compare(opened.count,priorOpen+1); compare(opened.signalArguments[priorOpen][0].kind,view); compare(activated.count,prior+1);
        }
    }
    function test_noNestedVerticalScrollerOrInventedSpinPicks() {
        makeCrate(mixed(16)); makeSpin();
        function visit(node) {
            if(node.flickableDirection!==undefined) verify(node.contentHeight<=node.height,"No inner vertical scroll range");
            for(var child of(node.children||[])) visit(child);
        }
        visit(crate); visit(spin);
        spin.songs=[]; tryCompare(spin,"pageCount",0); compare(spin.pageItems.length,0);
    }
}

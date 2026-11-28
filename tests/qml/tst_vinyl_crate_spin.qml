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
    Component {
        id: frostFixture
        Item {
            property alias paper: wallpaperImage
            property alias flick: scroll
            property alias surface: patch
            property alias readback: pixels
            width: 384; height: 320
            Canvas {
                id: pixels
                x: 400; width: 100; height: 72
                contextType: "2d"
            }
            Image {
                id: wallpaperImage
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                source: "data:image/svg+xml," + encodeURIComponent(
                    '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256">'
                    + '<path fill="red" d="M0 0H256V128H0Z"/>'
                    + '<path fill="blue" d="M0 128H256V256H0Z"/></svg>')
            }
            // Cover the fixture wallpaper visually so a missing sampled patch cannot pass.
            Rectangle { anchors.fill: parent; color: "black" }
            Vinyl.VinylWallpaperFrost { id: sharedBlur; wallpaper: wallpaperImage }
            Flickable {
                id: scroll
                anchors.fill: parent
                contentHeight: height + 200
                Vinyl.VinylFrostedSurface {
                    id: patch
                    // Starts below the colour boundary; scrolling moves it above.
                    x: 32; y: 220; width: 100; height: 72
                    frost: sharedBlur
                }
            }
        }
    }
    SignalSpy { id:activated; target:test.crate || test.spin; signalName:"activateRequested" }
    SignalSpy { id:opened; target:test.crate || test.spin; signalName:"openRequested" }
    SignalSpy { id:radio; target:test.crate || test.spin; signalName:"radioRequested" }
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
        verify(crate!==null); verify(waitForRendering(crate)); activated.clear(); opened.clear(); radio.clear(); pinned.clear();
    }
    function makeSpin() {
        var songs=[],albums=[],artists=[];
        for(var i=0;i<13;++i) songs.push(item("songs",i));
        for(i=0;i<9;++i) { albums.push(item("albums",i)); artists.push(item("artists",i)); }
        spin=spinFactory.createObject(page.contentItem,{x:20,y:200,width:1172,songs:songs,albums:albums,artists:artists,backdrop:wallpaper,pageFlick:page});
        verify(spin!==null); verify(waitForRendering(spin)); activated.clear(); opened.clear(); radio.clear(); feedback.clear();
    }
    function cleanup() {
        if(crate){crate.destroy();wait(0);crate=null;}
        if(spin){spin.destroy();wait(0);spin=null;}
        page.contentY=0; mouseMove(test,width-2,height-2);
    }
    function click(node,x,y) { verify(node!==null); verify(node.visible && node.enabled); mouseMove(node,x===undefined ? node.width/2 : x,y===undefined ? node.height/2 : y); verify(waitForRendering(node)); mouseClick(node,x,y); }
    function test_crateMixedSquareFacesInOneRowOfEight() {
        makeCrate(mixed(16)); compare(crate.objectName,"vinylCrate"); compare(crate.count,16);
        compare(crate.columns,8); compare(crate.pageSize,8); compare(crate.pageCount,2);
        var kinds=[];
        for(var i=0;i<8;++i){
            var tile=findChild(crate,"vinylCrateTile_"+i); verify(tile!==null); kinds.push(tile.kind);
            var face=findChild(tile,"vinylCrateTile_"+i+"_card_cover");
            verify(face !== null);
            compare(face.width,tile.width - 12); compare(face.height,face.width);
            compare(face.width,120);
            compare(face.artRadius,tile.kind==="artists" ? 60 : 10);
            var card=findChild(tile,"vinylCrateTile_"+i+"_card");
            compare(findChild(card,card.objectName+"_captionTitle").font.pixelSize,16);
            compare(findChild(card,card.objectName+"_captionDetail").font.pixelSize,13);
            verify(card.captionDetail.length>0);
            if(tile.kind==="songs") { compare(card.captionTitle,"Fixture "+i); compare(card.captionDetail,"Fixture artist · from Fixture album"); }
        }
        for(var kind of ["albums","artists","songs","mixes","radio"]) verify(kinds.indexOf(kind)>=0);
        var first=findChild(crate,"vinylCrateTile_0"), eighth=findChild(crate,"vinylCrateTile_7");
        compare(first.y,eighth.y);
        compare(findChild(crate,"vinylCrateTile_8"),null,"The Crate is a single row");
        compare(findChild(crate,"vinylCrateCaption"),null,"Hemanth removed the caption");
        var surface = findChild(first, first.objectName + "_frost");
        var film = checkFilm(surface, 13);
        var card = findChild(first, first.objectName + "_card");
        compare(card.x, 6); compare(card.y, 6); compare(first.height - card.height, 12);
        compare(surface.wallpaperTexture, null, "No wallpaper uses the fallback");
        mouseMove(first, 30, 30);
        tryCompare(film.border, "color", Qt.rgba(1, 1, 1, 0.44));
    }
    function checkFilm(surface, radius) {
        verify(surface && surface.visible);
        var film = findChild(surface, surface.objectName + "_film");
        verify(film);
        compare(film.radius, radius);
        compare(film.color, Qt.rgba(1, 1, 1, 0.10));
        compare(film.border.width, 1);
        compare(film.border.color, Qt.rgba(1, 1, 1, 0.18));
        return film;
    }
    function test_crateResumeUsesExactOwnerTrackAndGoldCaption() {
        makeCrate(mixed(16));
        var tile=findChild(crate,"vinylCrateTile_0"); compare(tile.resumeTrack,4);
        var caption=findChild(tile,"vinylCrateTile_0_card_captionDetail");
        compare(caption.text,"Resume · track 4 of 12"); compare(String(caption.color),"#f0c44a");
        var card=findChild(tile,"vinylCrateTile_0_card");
        click(card,20,100); compare(opened.count,1); compare(activated.count,0);
        card.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return); keyClick(Qt.Key_Space);
        compare(opened.count,3); compare(activated.count,0);
        var badge=findChild(card,"vinylCrateTile_0_card_albumPlay");
        compare(badge.accessibleName,"Resume album");
        tryCompare(badge,"revealed",true); click(badge);
        compare(opened.count,3); compare(activated.count,1);
        compare(activated.signalArguments[0][0].resumeTrack,4);
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
        verify(findChild(card, card.objectName + "_captionDetail").text.indexOf("✓") < 0, "No tick marks in captions");
        mouseClick(missing,20,100); compare(activated.count,0);
        verify(unpin.enabled,"Unavailable pinned tiles must allow unpin"); click(unpin); compare(pinned.count,2); compare(pinned.signalArguments[1][1],false);
        compare(missing.pinned,true); compare(JSON.stringify(rows),before); compare(activated.count,0);
    }
    function test_crateArtistOpensAndCentreStartsRadio() {
        makeCrate(mixed(16));
        var card=findChild(crate,"vinylCrateTile_1_card");
        click(card,20,100);
        compare(opened.count,1); compare(opened.signalArguments[0][0].id,"artists-1");
        compare(activated.count,0); compare(radio.count,0);
        var badge=findChild(card,"vinylCrateTile_1_card_artistRadio");
        tryCompare(badge,"revealed",true); click(badge);
        compare(radio.count,1); compare(radio.signalArguments[0][0].id,"artists-1");
        compare(opened.count,1); compare(activated.count,0);
    }
    function test_cratePagingAndNoTasteCard() {
        makeCrate(mixed(17)); compare(crate.pageCount,3); click(findChild(crate,"vinylCrateNext"));
        tryCompare(crate,"page",1); verify(findChild(crate,"vinylCrateTile_8")!==null); compare(findChild(crate,"vinylCrateTile_0"),null);
        click(findChild(crate,"vinylCratePrevious")); tryCompare(crate,"page",0);
        crate.items=[]; tryCompare(crate,"count",0);
        compare(findChild(crate,"vinylCrateTasteCard"),null); // the taste card is gone; the gate owns the picker
        crate.items=mixed(1); tryCompare(crate,"count",1);
        compare(findChild(crate,"vinylCrateTasteCard"),null);
    }
    function test_spinSongsTwelveCompactRowsAndPaging() {
        makeSpin(); compare(spin.view,"songs"); compare(spin.pageSize,12); compare(spin.pageCount,2);
        for(var i=0;i<12;++i) {var row=findChild(spin,"vinylSpinSong_"+i); verify(row!==null); compare(row.height,70);}
        var first=findChild(spin,"vinylSpinSong_0"), fourth=findChild(spin,"vinylSpinSong_3");
        var origin=first.mapToItem(spin,0,0), next=fourth.mapToItem(spin,0,0); compare(origin.x,next.x); verify(next.y>origin.y);
        var panel = findChild(spin, "vinylQuickPicksFrost");
        checkFilm(panel, 15);
        var panelInset = first.mapToItem(panel, 0, 0);
        compare(panelInset.x, 12); compare(panelInset.y, 10);
        compare(panel.height, 4 * 70 + 3 * 8 + 2 * 10);
        compare(first.children.find(child => child.width === 60).height, 60);
        compare(findChild(spin,"vinylSpinSong_12"),null);
        mouseMove(first,65,24);
        tryVerify(() => findChild(first,first.objectName+"_lineHighlight") !== null);
        var highlight=findChild(first,first.objectName+"_lineHighlight");
        compare(highlight.color,Qt.rgba(1,1,1,0.06));
        compare(highlight.border.width,0);
        compare(findChild(fourth,fourth.objectName+"_lineHighlight"),null);
        first.forceActiveFocus(Qt.TabFocusReason);
        compare(highlight.border.width,1);
        compare(highlight.border.color,"#f0c44a");
        keyClick(Qt.Key_Return); compare(activated.count,1); compare(activated.signalArguments[0][0].id,"songs-0");
        click(findChild(spin,"vinylSpinNext")); tryCompare(spin,"page",1); verify(findChild(spin,"vinylSpinSong_12")!==null);
        compare(findChild(spin,"vinylSpinSong_0"),null);
    }
    function test_spinSwitchKeepsCaptionsAndPreservesOuterScroll() {
        makeSpin(); page.contentY=120; var before=page.contentY;
        for(var view of ["albums","artists","songs"]){
            click(findChild(spin,"vinylSpinSwitch_"+view)); tryCompare(spin,"view",view);
            compare(findChild(spin,"vinylSpinSwitch").view,view); compare(page.contentY,before);
            if(view!=="songs") {
                tryVerify(() => !findChild(spin, "vinylQuickPicksFrost"));
                var card=findChild(spin,"vinylSpinTile_0"); verify(card!==null); compare(card.presentationKind,view);
                var detail=view==="albums" ? "Fixture artist" : "";
                compare(card.captionDetail,detail);
                compare(card.accessibleName, card.captionTitle + (detail ? ". " + detail : ""));
                if (view === "artists") compare(card.accessibleDescription,"Close to your city pop");
                card.exposureRail=page;
                compare(card.captionDetail,detail);
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
    function test_spinAlbumArtistPagingAndSeparateCentreAction_data() {
        return [{tag:"albums", view:"albums"}, {tag:"artists", view:"artists"}];
    }
    function test_spinAlbumArtistPagingAndSeparateCentreAction(data) {
        makeSpin();
        click(findChild(spin,"vinylSpinSwitch_"+data.view)); tryCompare(spin,"view",data.view);
        click(findChild(spin,"vinylSpinNext")); tryCompare(spin,"page",1);
        verify(findChild(spin,"vinylSpinTile_8")!==null); compare(findChild(spin,"vinylSpinTile_0"),null);
        click(findChild(spin,"vinylSpinPrevious")); tryCompare(spin,"page",0);
        var card=findChild(spin,"vinylSpinTile_0"), isArtist=data.view==="artists";
        var secondary=isArtist ? radio : activated;
        click(card,20,100);
        compare(opened.count,1); compare(opened.signalArguments[0][0].id,data.view+"-0");
        card.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return); keyClick(Qt.Key_Space);
        compare(opened.count,3); compare(secondary.count,0);
        var badge=findChild(card,card.objectName+(isArtist ? "_artistRadio" : "_albumPlay"));
        compare(badge.accessibleName,isArtist ? "Artist radio" : "Play album");
        tryCompare(badge,"revealed",true); click(badge);
        compare(secondary.count,1); compare(secondary.signalArguments[0][0].id,data.view+"-0");
        compare(opened.count,3);
        compare((isArtist ? activated : radio).count,0);
    }
    function test_noNestedVerticalScrollerOrInventedSpinPicks() {
        makeCrate(mixed(16)); makeSpin();
        function visit(node) {
            if(node.flickableDirection!==undefined) verify(node.contentHeight<=node.height,"No inner vertical scroll range");
            for(var child of(node.children||[])) visit(child);
        }
        visit(crate); visit(spin);
        spin.songs=[]; tryCompare(spin,"pageCount",0); compare(spin.pageItems.length,0);
        tryVerify(() => !findChild(spin, "vinylQuickPicksFrost"));
    }
    function test_wallpaperPatchFollowsScrollOrFallsBackOnSoftware() {
        var fixture = createTemporaryObject(frostFixture, test);
        verify(fixture);
        var patch = fixture.surface;
        tryCompare(fixture.paper, "status", Image.Ready);
        // grabImage() reads the window swapchain, which isn't available on the
        // Windows offscreen RHI runner. Grab the item to its own image instead.
        function pixel(x, y) {
            var image = null;
            verify(patch.grabToImage(result => image = result));
            tryVerify(() => image !== null);
            var canvas = fixture.readback;
            canvas.loadImage(image.url);
            tryVerify(() => canvas.isImageLoaded(image.url));
            var ctx = canvas.getContext("2d");
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.drawImage(image.url, 0, 0, canvas.width, canvas.height);
            var rgba = ctx.getImageData(x, y, 1, 1).data;
            canvas.unloadImage(image.url);
            return Qt.rgba(rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, rgba[3] / 255);
        }
        function center() { return pixel(50, 36); }
        if (patch.GraphicsInfo.api === GraphicsInfo.Software) {
            compare(patch.wallpaperTexture, null);
        } else {
            console.info("Checking the rendered wallpaper patch on graphics API", patch.GraphicsInfo.api);
            verify(patch.wallpaperTexture);
            compare(patch.wallpaperOrigin, Qt.point(32, 220));
            tryVerify(() => { var c = center(); return c.b > 0.8 && c.r < 0.2; }, 5000,
                      "Below the boundary the patch must sample blue");
            var texture = patch.wallpaperTexture;
            var captures = 0;
            texture.scheduledUpdateCompleted.connect(() => ++captures);
            fixture.flick.contentY = 96;
            tryCompare(patch, "wallpaperOrigin", Qt.point(32, 124));
            tryVerify(() => { var c = center(); return c.r > 0.2 && c.b > 0.2; }, 5000,
                      "At the boundary both colours must mix in the shared blur");
            fixture.flick.contentY = 200;
            tryCompare(patch, "wallpaperOrigin", Qt.point(32, 20));
            tryVerify(() => { var c = center(); return c.r > 0.8 && c.b < 0.2; }, 5000,
                      "Above the boundary the same patch must sample red");
            compare(patch.wallpaperTexture, texture);
            compare(captures, 0, "Scrolling never regenerates the shared texture");
            var corner = pixel(0, 0);
            verify(corner.r < 0.1 && corner.b < 0.1, "The wallpaper must not fill the rounded corner");

            fixture.flick.contentY = 96;
            fixture.height = 448;
            tryVerify(() => { var c = center(); return c.r > 0.8 && c.b < 0.2; }, 5000,
                      "Resizing resamples the wallpaper crop");
            compare(captures, 1);
            fixture.paper.source = String(fixture.paper.source).replace("red", "lime");
            tryVerify(() => { var c = center(); return c.g > 0.8 && c.r < 0.2; }, 5000,
                      "Changing the source replaces the cached blur");
            compare(captures, 2);
        }
        fixture.paper.source = "";
        tryCompare(patch, "wallpaperTexture", null);
        tryVerify(() => { var c = center(); return Math.abs(c.r - 0.10) < 0.02 && c.r === c.g && c.g === c.b; },
                  5000, "Unloaded wallpaper leaves the .10 film over black");
    }
}

import QtQuick
import QtTest
import "../../qml" as House
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylWidgetIntegration"
    visible: true
    width: 600; height: 700
    when: windowShown && faceFont.status === FontLoader.Ready
    FontLoader { id: faceFont; source: "../../assets/fonts/Fraunces-Regular.ttf" }
    QtObject {
        id: worldStub
        property var session: null
        property var activated: null
        property int activationCount: 0
        property var requested: null
        property var feedbackCalls: []
        function requestRecommendationActions(item,row) {
            if(Number(row.profileGeneration)!==0) return false;
            requested={item:item,row:row};return true;
        }
        function artwork(cover) { return String(cover || "").indexOf("file:") === 0 ? cover : cover ? "file:///resolved/" + cover : ""; }
        function activate(item) { activated = item; activationCount++; }
        function setRecommendationFeedback(ref, action, generation) { feedbackCalls=feedbackCalls.concat([{ref:ref,action:action,generation:generation}]); return true; }
        function recommendationProfileGeneration() { return 0; }
        function recommendationArtistRef(artist) { return null; }
    }
    House.Theme { id: theme }
    Component { id: factory; Vinyl.VinylItemCard { objectName: "fixtureCard"; worldData: worldStub } }
    Component { id: catalogueFactory; Vinyl.VinylArtistCard { width: 196; artistData: worldStub } }
    QtObject {
        id: libraryStub
        property string section: "artists"
        property var artists: [{id:"fixture-artist", title:"Visible library artist", cover:""}]
        property var activated: null
        function activateArtist(artist) { activated = artist; }
    }
    Component { id: libraryFactory; Vinyl.VinylPersonalLibraryPage {
        width: test.width; libraryData: libraryStub; backdrop: null
    } }
    function labelCount(node, value) {
        var count = node.visible && node.text === value ? 1 : 0;
        for (var i = 0; node.children && i < node.children.length; ++i)
            count += labelCount(node.children[i], value);
        return count;
    }
    function test_albumTileHasNoFillOrEdge() {
        mouseMove(test, width - 2, height - 2);
        const card = createTemporaryObject(factory, test, {item:{kind:"albums", title:"Album"}});
        const tile = findChild(card, "fixtureCard_tile");
        verify(tile !== null);
        verify(!(tile instanceof Rectangle) || tile.color.a === 0, "The album tile must not have a fill");
        verify(!(tile instanceof Rectangle) || tile.border.width === 0 || tile.border.color.a === 0,
            "The album tile must not have an edge");
        verify(!tile.layer.enabled, "The album tile must not use a layer for glass blur");
        compare(findChild(card, "fixtureCard_artRing"), null);
    }
    function test_libraryArtistKeepsHostCaptionAndActivation() {
        var page = createTemporaryObject(libraryFactory, test);
        verify(page !== null);
        var card = findChild(page, "vinylLibraryArtist_0");
        verify(card !== null && card.visible);
        compare(labelCount(card, "Visible library artist"), 1);
        libraryStub.activated = null;
        mouseClick(card, card.width / 2, card.height / 2);
        compare(libraryStub.activated, libraryStub.artists[0]);
    }
    function test_hoverFeedbackBadgesAreDirectAndDoNotNavigate() {
        var source={id:"feedback",kind:"songs",title:"Track",artist:"Singer",ref:{itemId:"track"}};
        var card=createTemporaryObject(factory,test,{x:30,y:20,item:source,
            recommendationRow:{recommendation:true,key:"quick-picks",profileGeneration:0}});
        var like=findChild(card,"vinylRecommendationLike_feedback");
        var hide=findChild(card,"vinylRecommendationHide_feedback");
        verify(like!==null && hide!==null);
        mouseMove(test,590,650); test.forceActiveFocus(Qt.OtherFocusReason);
        tryCompare(like,"opacity",0); compare(hide.opacity,0);
        verify(!card.contextEnabled && !like.contextEnabled && !hide.contextEnabled);
        worldStub.feedbackCalls=[]; worldStub.activationCount=0; worldStub.requested=null;
        mouseClick(card,60,60,Qt.RightButton);
        compare(worldStub.feedbackCalls.length,0); compare(worldStub.requested,null);
        compare(findChild(card,"vinylRecommendationFeedbackMenu_feedback"),null);
        mouseMove(card,card.width/2,40);
        tryCompare(like,"opacity",1); compare(hide.opacity,1);
        mouseClick(like,15,15);
        compare(worldStub.feedbackCalls.length,1);
        compare(worldStub.feedbackCalls[0].action,"like");
        compare(worldStub.feedbackCalls[0].ref,source.ref);
        mouseClick(hide,15,15);
        compare(worldStub.feedbackCalls.length,2);
        compare(worldStub.feedbackCalls[1].action,"hide");
        compare(worldStub.activationCount,0,"Feedback never activates the underlying card");
        compare(worldStub.requested,null,"Feedback does not navigate to preferences");
        mouseMove(test,590,650); test.forceActiveFocus(Qt.OtherFocusReason);
        tryCompare(like,"opacity",0);
        like.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(like,"opacity",1);
        keyClick(Qt.Key_Return);
        compare(worldStub.feedbackCalls.length,3);
        card.recommendationRow={recommendation:true,key:"quick-picks",profileGeneration:1};
        verify(!card.submitFeedback("hide")); compare(worldStub.feedbackCalls.length,3);
        card.recommendationRow={key:"taste-artists",kind:"artists",profileGeneration:0};
        tryVerify(() => !findChild(card,"vinylRecommendationLike_feedback"));
        tryVerify(() => !findChild(card,"vinylRecommendationHide_feedback"));
    }
    function test_catalogueMetadataAndSizing() {
        var song = {track:{artists:[null, "Singer"],title:"Track"},album:{artists:["Album artist"]}};
        var card = createTemporaryObject(catalogueFactory, test, {record:{title:"Track",subtitle:"Singer",song:song}});
        verify(card !== null);
        compare(card.secondaryText, "Singer");
        compare(card.artHeight, 196);
        verify(card.implicitHeight > card.artHeight);
        compare(labelCount(card, "Singer"), 1);
        compare(labelCount(card, "Track"), 1);
        card.record = {title:"Artist"}; card.shape = "round";
        compare(card.focusRadius, 98);
        compare(labelCount(card, "Artist"), 1);
        const fallback = findChild(card, "vinylArtworkFallback");
        verify(fallback.visible);
        compare(fallback.color, theme.vaultSurface);
        compare(fallback.border.width, 0);
        compare(fallback.radius, fallback.width / 2);
        card.shape = "wide";
        compare(card.artHeight, 196 * 9 / 16);
    }
    function test_songArtworkAndLabelActivateOriginalItemOnce() {
        var track={id:"recording-one",title:"Song title",artists:["Singer"],available:true};
        var album={id:"album-one",title:"Album title",artists:["Singer"],tracks:[track]};
        var source={id:track.id,kind:"songs",title:track.title,artist:"Singer",subtitle:"Singer",
            reason:"From your listening",cover:String(Qt.resolvedUrl("../../assets/icons/play.svg")),albumId:album.id,trackId:track.id,trackIndex:0,
            ref:{identity:"provider",kind:"recording",providerId:"youtube-music",itemId:track.id},
            destination:{kind:"song",id:track.id,playable:true},record:album};
        var card=createTemporaryObject(factory,test,{x:30,y:20,item:source});
        verify(card !== null);
        compare(card.artistText,"Singer");
        compare(card.secondaryText,"From your listening");
        tryCompare(findChild(card,"vinylArtworkImage"),"status",Image.Ready);
        compare(labelCount(card,"Song title"),1);
        compare(labelCount(card,"Singer"),1);
        worldStub.activationCount=0;
        mouseClick(card,card.width/2,40);
        compare(worldStub.activationCount,1,"Cover click activates exactly once");
        compare(worldStub.activated,source,"Cover preserves the complete original source item");
        worldStub.activationCount=0;
        mouseClick(card,card.width/2,card.captionTop+10);
        compare(worldStub.activationCount,1,"Caption click activates exactly once");
        compare(worldStub.activated,source);
    }
    function test_metadataAndActivation() {
        var item = {id:"song", kind:"songs", title:"Song title", subtitle:"Artist name",
            record:{artists:["Artist name"]}, reason:"From your listening", unavailable:true};
        var card = createTemporaryObject(factory, test, {item:item});
        verify(card !== null);
        compare(card.artistText, "Artist name");
        compare(card.secondaryText, "Unavailable\nFrom your listening");
        compare(card.captionTitle, "Song title");
        compare(card.captionDetail, "Unavailable");
        verify(!card.enabled);
        compare(card.accessibleName, "Song title. Unavailable");
        compare(card.implicitHeight, 251);
        card.item = Object.assign({}, item, {unavailable:false, subtitle:"Album name"});
        compare(card.secondaryText, "From your listening\nAlbum name");
        card.triggered();
        compare(worldStub.activated.id, "song");
    }
    function test_kindAndArtworkReplacement() {
        var card = createTemporaryObject(factory, test, {item:{id:"mix",kind:"mixes",title:"Mix",subtitle:"Listen again",
            artworkCovers:["one.jpg","two.jpg","three.jpg","four.jpg","five.jpg"]}});
        verify(card !== null);
        compare(card.mixArtworkSources.length, 4);
        compare(String(card.mixArtworkSources[1]), "file:///resolved/two.jpg");
        compare(card.captionDetail, "Listen again");
        card.item = {id:"artist",kind:"artists",title:"Artist",subtitle:"12 songs",reason:"Like Mariya Takeuchi"};
        compare(card.presentationKind, "artists");
        compare(card.captionDetail, "Artist");
        compare(card.secondaryText, "Like Mariya Takeuchi\n12 songs");
        card.item = {id:"resume",kind:"resume",title:"Track",artist:"Real artist",subtitle:"Real artist"};
        compare(card.presentationKind, "songs");
        compare(card.captionDetail, "Real artist");
        compare(card.secondaryText, "");
    }
    function test_longTitleAndArtistElideWithoutChangingTileGeometry() {
        var card = createTemporaryObject(factory, test, {width:130,item:{kind:"songs", title:"Short title", artist:"Short artist"}});
        const title = findChild(card, "fixtureCard_captionTitle");
        const detail = findChild(card, "fixtureCard_captionDetail");
        compare(card.height, 185);
        const captionTop = card.captionTop;
        card.item = {kind:"songs", title:"An exceptionally long title that will never fit on a single line",
            artist:"An exceptionally long artist name that also needs an ellipsis"};
        tryVerify(() => title.truncated && detail.truncated);
        compare(title.lineCount, 1);
        compare(detail.lineCount, 1);
        compare(card.captionTop, captionTop);
        compare(card.height, 185);
    }
}

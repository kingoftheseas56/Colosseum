import QtQuick
import QtTest
import QtQuick.Controls
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylWidgetIntegration"
    visible: true
    width: 600; height: 700
    when: windowShown
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
    Component { id: factory; Vinyl.VinylItemCard { width: 196; worldData: worldStub } }
    Component { id: catalogueFactory; Vinyl.VinylArtistCard { width: 196; artistData: worldStub } }
    Component {
        id: songWidgetFactory
        Vinyl.VinylSongWidget {
            width: 196
            artworkSource: Qt.resolvedUrl("../../assets/icons/play.svg")
        }
    }
    function test_hoverFeedbackBadgesAreDirectAndDoNotNavigate() {
        var source={id:"feedback",kind:"songs",title:"Track",artist:"Singer",ref:{itemId:"track"}};
        var card=createTemporaryObject(factory,test,{x:30,y:20,item:source,
            recommendationRow:{recommendation:true,key:"quick-picks",profileGeneration:0}});
        card.height=card.implicitHeight;
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
        verify(!like.visible && !hide.visible,"Chosen artists are edited separately from recommendation feedback");
    }
    function test_catalogueMetadataAndSizing() {
        var song = {track:{artists:[null, "Singer"],title:"Track"},album:{artists:["Album artist"]}};
        var card = createTemporaryObject(catalogueFactory, test, {record:{title:"Track",subtitle:"Singer",song:song}});
        verify(card !== null);
        compare(card.secondaryText, "");
        compare(card.implicitHeight, card.artHeight, "An embedded artist must not leave an empty caption block");
        verify(card.artHeight >= 256);
        card.record = {title:"Track",subtitle:"Album name",song:song};
        compare(card.secondaryText, "Album name");
        verify(card.implicitHeight > card.artHeight);
        card.record = {title:"Artist"}; card.shape = "round";
        compare(card.focusRadius, 19.6);
        compare(card.implicitHeight, card.artHeight);
        card.record = {title:"Artist",subtitle:"Related to your listening"};
        verify(card.hasCaption, "Artist secondary metadata must remain outside the embedded name");
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
        card.height=card.implicitHeight;
        wait(30);
        compare(card.artistText,"Singer");
        compare(card.secondaryText,"From your listening");
        var artwork=findChild(card,card.objectName+"_cover");
        tryCompare(artwork,"status",Loader.Ready);
        tryCompare(findChild(artwork.item,"vinylArtworkImage"),"status",Image.Ready);
        compare(artwork.item.title,"Song title");
        compare(artwork.item.artist,"Singer");
        worldStub.activationCount=0;
        mouseClick(card,card.width/2,artwork.height/3);
        compare(worldStub.activationCount,1,"Cover click activates exactly once");
        compare(worldStub.activated,source,"Cover preserves the complete original source item");
        worldStub.activationCount=0;
        mouseClick(card,card.width/2,artwork.height-25);
        compare(worldStub.activationCount,1,"Paper title/artist label click activates exactly once");
        compare(worldStub.activated,source,"Label preserves the complete original source item");
        compare(worldStub.activated.ref,source.ref);
        compare(worldStub.activated.destination,source.destination);
    }
    function test_metadataAndActivation() {
        var item = {id:"song", kind:"songs", title:"Song title", subtitle:"Artist name",
            record:{artists:["Artist name"]}, reason:"From your listening", unavailable:true, cover:"cover.jpg"};
        var card = createTemporaryObject(factory, test, {item:item});
        verify(card !== null);
        compare(card.artistText, "Artist name");
        compare(card.secondaryText, "Unavailable\nFrom your listening");
        verify(card.includesTitle);
        verify(card.implicitHeight > 256, "Paper plus recommendation/availability must contribute to shelf height");
        compare(String(card.artworkSource), "file:///resolved/cover.jpg");
        card.item = Object.assign({}, item, {unavailable:false, subtitle:"Album name"});
        compare(card.secondaryText, "From your listening\nAlbum name");
        card.triggered();
        compare(worldStub.activated.id, "song");
    }
    function test_kindAndArtworkReplacement() {
        var card = createTemporaryObject(factory, test, {item:{id:"mix",kind:"mixes",title:"Mix",
            artworkCovers:["one.jpg","two.jpg","three.jpg","four.jpg"]}});
        verify(card !== null);
        compare(card.mixArtworkSources.length, 3);
        compare(String(card.mixArtworkSources[1]), "file:///resolved/two.jpg");
        compare(card.presentationKind, "mixes");
        card.item = {id:"artist",kind:"artists",title:"Artist",subtitle:"12 songs"};
        compare(card.presentationKind, "artists");
        compare(card.secondaryText, "12 songs");
        verify(card.includesTitle);
        card.item = {id:"resume",kind:"resume",title:"Track",artist:"Real artist",subtitle:"Real artist"};
        compare(card.presentationKind, "songs");
        compare(card.artistText, "Real artist");
        compare(card.secondaryText, "");
        card.item = {id:"station",kind:"radio",title:"Station"};
        compare(card.presentationKind, "radio");
        verify(!card.includesTitle);
    }
    function test_songWidgetLabelGeometryReservesTwoTitleLinesAndArtistRow() {
        var samples = [
            {title:"Replay", artist:"Singer"},
            {title:"A song title that wraps onto a second line", artist:"Singer"},
            {title:"An exceptionally long song title that must keep wrapping and then elide at the second line without changing the paper dimensions", artist:"A very long artist name that still occupies one fixed row"},
            {title:"", artist:""}
        ];
        var widths = [196, 280];
        for (var wi = 0; wi < widths.length; wi++) {
            var reference = null;
            for (var si = 0; si < samples.length; si++) {
                var widget = createTemporaryObject(songWidgetFactory, test, {
                    x: 8, y: 8, width: widths[wi],
                    title: samples[si].title, artist: samples[si].artist
                });
                verify(widget !== null);
                widget.height = widget.implicitHeight;
                var artwork = findChild(widget, "vinylSongArtwork");
                var image = findChild(artwork, "vinylArtworkImage");
                var paper = findChild(widget, "vinylSongPaper");
                var title = findChild(widget, "vinylSongTitle");
                var artist = findChild(widget, "vinylSongArtist");
                verify(artwork !== null && image !== null && paper !== null && title !== null && artist !== null);
                compare(artwork.width, widget.coverSize);
                compare(artwork.height, widget.coverSize, "Cover remains square at width " + widths[wi]);
                compare(artwork.crop, false);
                compare(image.fillMode, Image.PreserveAspectFit, "Artwork remains uncropped");
                compare(paper.x, widget.inset);
                compare(paper.y, artwork.y + artwork.height, "Paper begins below the full cover");
                compare(paper.height, widget.labelHeight);
                compare(title.height, widget.titleAreaHeight);
                compare(title.verticalAlignment, Text.AlignVCenter);
                compare(artist.height, widget.artistRowHeight);
                verify(title.y + title.height < artist.y, "Reserved title area must not overlap the artist row");
                verify(title.lineCount <= 2, "Titles stay within two lines");
                if (si === 0)
                    compare(title.lineCount, 1, "Short title uses one line");
                if (si === 1)
                    compare(title.lineCount, 2, "Medium title uses both reserved lines");
                if (si === 2) {
                    compare(title.lineCount, 2, "Long title is capped at two visible lines");
                    verify(title.truncated, "Long title keeps the existing ellipsis behavior");
                }
                if (si === 3) {
                    compare(title.text, "");
                    compare(artist.text, "");
                }
                var artistY = artist.mapToItem(widget, 0, 0).y;
                if (!reference) {
                    reference = {
                        implicitHeight: widget.implicitHeight,
                        labelHeight: widget.labelHeight,
                        titleHeight: title.height,
                        artistHeight: artist.height,
                        artistY: artistY
                    };
                } else {
                    compare(widget.implicitHeight, reference.implicitHeight,
                        "Total card height stays uniform at width " + widths[wi]);
                    compare(widget.labelHeight, reference.labelHeight,
                        "Paper strip height stays uniform at width " + widths[wi]);
                    compare(title.height, reference.titleHeight,
                        "Title area stays uniform at width " + widths[wi]);
                    compare(artist.height, reference.artistHeight,
                        "Artist row stays reserved at width " + widths[wi]);
                    compare(artistY, reference.artistY,
                        "Artist row position stays uniform at width " + widths[wi]);
                }
            }
        }
    }
}

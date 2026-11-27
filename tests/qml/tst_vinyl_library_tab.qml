import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylLibraryTab"
    when: windowShown
    visible: true
    width: 1120
    height: 700
    property var library: null
    property var ownerData: null
    readonly property var expected: ["playlists", "saved-albums", "liked-songs", "followed-artists", "recently-played"]
    Rectangle { id: wallpaper; anchors.fill: parent; color: "#11131a" }
    Flickable {
        id: hostScroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: test.library ? test.library.implicitHeight : 0
    }
    Component { id: libraryFactory; Vinyl.VinylLibraryTab {} }
    Component {
        id: dataFactory
        QtObject {
            property QtObject personalLibrary: QtObject { signal importFolderRequested() }
            property var session: null
            property string persistenceError: ""
            property int activationCalls: 0
            property int feedbackCalls: 0
            property int contextCalls: 0
            property var lastFeedback: null
            function artwork(value) { return value; }
            function activate(item) { activationCalls++; }
            function recommendationProfileGeneration() { return 7; }
            function recommendationArtistRef(item) { return item.ref; }
            function setRecommendationFeedback(ref, action, generation) { feedbackCalls++; lastFeedback={ref:ref,action:action,generation:generation}; return true; }
            function requestRecommendationActions(item, row) { contextCalls++; return true; }
            function recordVisibleRecommendation() { return false; }
        }
    }
    SignalSpy { id: imported; target: test.ownerData ? test.ownerData.personalLibrary : null; signalName: "importFolderRequested" }
    SignalSpy { id: activated; target: test.library; signalName: "itemActivated" }
    SignalSpy { id: listed; target: test.library; signalName: "listRequested" }
    function item(kind, id) {
        return {kind:kind, id:id, title:"Owner " + id, artist:"Fixture artist", subtitle:"Owner metadata",
            cover:Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg"),
            ref:{identity:"provider",kind:"artist",provider:"youtube-music",providerId:"UC"+"A".repeat(22)}};
    }
    function rows() {
        return [
            {key:"recently-played",title:"Recently played",items:[item("songs","recent-1")]},
            {key:"downloads",title:"Downloads",items:[]},
            {key:"followed-artists",title:"Artists you follow",items:[item("artists","artist-1")]},
            {key:"liked-songs",title:"Liked songs",items:[item("songs","song-1")]},
            {key:"saved-albums",title:"Saved albums",items:[item("albums","album-1")]},
            {key:"playlists",title:"Playlists",items:[item("playlists","playlist-1")]}
        ];
    }
    function init() {
        ownerData = dataFactory.createObject(test);
        library = libraryFactory.createObject(hostScroll.contentItem,
            {width:hostScroll.width, rows:rows(), worldData:ownerData, backdrop:wallpaper, pageFlick:hostScroll});
        verify(library !== null);
        verify(waitForRendering(library));
        activated.clear(); listed.clear(); hostScroll.contentY=0;
    }
    function cleanup() {
        if (library) { library.destroy(); wait(0); library=null; }
        if (ownerData) { ownerData.destroy(); wait(0); ownerData=null; }
    }
    function reveal(target) {
        var at = target.mapToItem(hostScroll.contentItem,0,0);
        hostScroll.contentY = Math.max(0,Math.min(hostScroll.contentHeight-hostScroll.height,at.y-30));
        verify(waitForRendering(target));
    }
    function hasVisibleText(node, text) {
        if (node.visible && node.text === text) return true;
        for (var child of (node.children || [])) if (hasVisibleText(child,text)) return true;
        return false;
    }
    function test_fixedOrderAndVisibleOwnerTitles() {
        compare(library.objectName,"vinylLibrary");
        compare(library.rowKeys,expected.join(","));
        for (var key of expected) {
            var row=findChild(library,"vinylLibraryRow_"+key);
            verify(row !== null && row.visible);
            compare(row.itemCount,1);
            verify(hasVisibleText(row,row.row.title));
        }
    }
    function test_nonemptyDownloadsKeepsAllSixRowsInApprovedOrder() {
        var source=rows();
        source[1]={key:"downloads",title:"Downloads",items:[item("songs","download-1")]};
        library.rows=source;
        tryCompare(library,"rowKeys","playlists,saved-albums,liked-songs,followed-artists,downloads,recently-played");
        var row=findChild(library,"vinylLibraryRow_downloads");
        verify(row !== null && row.visible);
        compare(row.itemCount,1);
        verify(hasVisibleText(row,"Downloads"));
    }
    function test_emptyAndMissingRowsStayAbsent() {
        compare(findChild(library,"vinylLibraryRow_downloads"),null);
        library.rows=[{key:"liked-songs",title:"Liked songs",items:[]}];
        tryCompare(library,"rowKeys","");
        verify(library.implicitHeight >= 38, "The empty Library retains its import header.");
        compare(findChild(library,"vinylLibraryRow_liked-songs"),null);
    }
    function test_importFolderStaysVisibleWithNoRows() {
        library.rows = [];
        tryCompare(library, "rowKeys", "");
        var action = findChild(library, "vinylLibraryImportFolder");
        verify(action !== null);
        verify(action.visible && action.enabled);
        compare(action.width, 38);
        compare(action.height, 38);
        compare(action.label, "Add music folder");
        compare(action.symbol, "musicFolder");
        compare(action.backdrop, wallpaper);
        compare(action.mapToItem(library, 0, 0).x, library.width - 38);
        compare(action.mapToItem(library, 0, 0).y, 0);
        verify(hasVisibleText(library, "Albums from your library will appear here."));
        imported.clear();
        mouseClick(action);
        compare(imported.count, 1);
        ownerData.personalLibrary = null;
        action.triggered();
        compare(imported.count, 1);
        library.worldData = null;
        action.triggered();
        compare(imported.count, 1);
    }
    function test_importFolderAlsoVisibleWithRows() {
        var action = findChild(library, "vinylLibraryImportFolder");
        verify(action !== null && action.visible);
        verify(!hasVisibleText(library, "Albums from your library will appear here."));
        imported.clear();
        mouseClick(action);
        compare(imported.count, 1);
    }
    function test_pointerActivationAndSeeAllAreOwnerSignalsOnly() {
        var card=findChild(library,"vinylCard_playlists_0");
        reveal(card); mouseClick(card);
        compare(activated.count,1);
        compare(activated.signalArguments[0][0].id,"playlist-1");
        compare(ownerData.activationCalls,0);
        var more=findChild(library,"vinylShelfSeeAll_playlists");
        reveal(more); mouseClick(more);
        compare(listed.count,1);
        compare(listed.signalArguments[0][0].key,"playlists");
    }
    function test_ownerListenUpdateAddsRecentlyPlayedWithoutInventingOtherRows() {
        library.rows=[{key:"saved-albums",title:"Saved albums",items:[item("albums","album-1")]}];
        tryCompare(library,"rowKeys","saved-albums");
        var next=library.rows.slice();
        next.push({key:"recently-played",title:"Recently played",items:[item("songs","owner-listen-2")]});
        library.rows=next;
        tryCompare(library,"rowKeys","saved-albums,recently-played");
        var card=findChild(library,"vinylCard_recently-played_0");
        verify(card !== null);
        compare(card.item.id,"owner-listen-2");
        compare(findChild(library,"vinylLibraryRow_liked-songs"),null);
        compare(findChild(library,"vinylLibraryRow_downloads"),null);
    }
    function test_ownerObjectsAreNotMutatedAndUnknownRowsAreIgnored() {
        var source=rows();source.push({key:"imported-files",title:"Imported files",items:[item("songs","local") ]});
        var before=JSON.stringify(source);
        for (var row of source) { Object.freeze(row.items); Object.freeze(row); }
        Object.freeze(source);
        library.rows=source;
        tryCompare(library,"rowKeys",expected.join(","));
        compare(JSON.stringify(source),before);
        compare(findChild(library,"vinylLibraryRow_imported-files"),null);
    }
    function test_squareArtworkAndIntegratedCaptions() {
        for (var pair of [["playlists", "playlists"],["saved-albums","albums"],["liked-songs","songs"],["followed-artists","artists"]]) {
            var card=findChild(library,"vinylCard_"+pair[0]+"_0");
            compare(card.presentationKind,pair[1]);
            var art=findChild(card,card.objectName+"_cover");
            verify(art !== null);
            compare(art.height,art.width);
            verify(findChild(art,"vinylArtworkImage") !== null);
            if (pair[1] === "artists") verify(findChild(art,"vinylArtistRing") !== null);
            compare(card.captionTitle, card.item.title);
        }
    }
    function test_existingFeedbackForwardsOnlyToSuppliedOwner() {
        library.rows=[{key:"followed-artists",title:"Artists you follow",items:[item("artists","feedback-artist")],
                      recommendation:true, profileGeneration:7}];
        var card=findChild(library,"vinylCard_followed-artists_0");
        verify(card !== null && card.feedbackAvailable);
        reveal(card); mouseMove(card,50,50);
        var like=findChild(card,"vinylRecommendationLike_feedback-artist");
        tryCompare(like,"revealed",true);
        mouseClick(like);
        compare(ownerData.feedbackCalls,1);
        compare(ownerData.lastFeedback.action,"like");
        compare(ownerData.lastFeedback.generation,7);
        compare(ownerData.lastFeedback.ref.providerId,card.item.ref.providerId);
        verify(card.openRecommendationActions());
        compare(ownerData.contextCalls,1);
        library.worldData=null;
        tryCompare(card,"feedbackAvailable",false);
    }
    function test_noNestedVerticalScroller() {
        function visit(node) {
            if (node.contentHeight !== undefined && node.contentWidth !== undefined && node.flickableDirection !== undefined)
                verify(node.contentHeight <= node.height + 1,"Library rail must have no vertical scroll range");
            for (var child of (node.children || [])) visit(child);
        }
        visit(library);
    }
}

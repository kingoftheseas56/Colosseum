import QtQuick
import QtTest
import "../../qml/vinyl"

TestCase {
    id: testCase
    name: "VinylArtistVideo"
    when: windowShown
    width: 1280; height: 720; visible: true

    readonly property string clipId: "abcDEf123_-"
    readonly property string clipTitle: "Fixture music video"
    readonly property string watchUrl: "https://music.youtube.com/watch?v=" + clipId

    QtObject {
        id: provider
        property var artist: ({})
        property var artistAlbums: []
        property var artistSingles: []
        property var artistMedia: ({videos:[{videoId:testCase.clipId, title:testCase.clipTitle, seconds:175}]})
        property var artistArtwork: ({photos:[]})
        property bool artistBusy: false
        property bool cached: false
        property string artistError: ""
        function openArtist(intent) {}
    }
    VinylLiveArtistData { id: live; catalogueService: provider; reducedMotion: true }
    VinylArtistData {
        id: preview
        name: "Fixture Artist"
        reducedMotion: true
        mediaCatalogue: ({"Fixture Artist": {name:name, provider:"YouTube Music",
            sourceUrl:"https://music.youtube.com/channel/fixture",
            videos:[{id:testCase.clipId, title:testCase.clipTitle, sourceUrl:testCase.watchUrl}]}})
    }
    QtObject { id: world; function artwork(url) { return url || ""; } }
    Rectangle { id: background; anchors.fill: parent; color: "#101015" }
    VinylArtistPage { id: page; anchors.fill: parent; artistData:live; worldData:world; backdrop:background }
    SignalSpy { id: play; target: page; signalName: "playVideoRequested" }

    function initTestCase() {
        live.open({id:"ytm:artist:UCaaaaaaaaaaaaaaaaaaaaaa", name:"Fixture Artist"});
    }
    function init() {
        page.artistData = live;
        play.clear();
    }
    function videoTile() {
        page.switchTab("videos", true);
        const tile = findChild(page, "vinylArtistVideoCard_0");
        verify(tile);
        tryCompare(tile, "visible", true);
        verify(waitForRendering(tile));
        page.reveal(tile);
        return tile;
    }
    function mediaDialog() {
        const dialog = findChild(page, "vinylArtistMediaDialog");
        verify(dialog);
        return dialog;
    }
    function assertPlaybackRequested() {
        compare(play.count, 1);
        compare(play.signalArguments[0][0].videoId, clipId);
        compare(play.signalArguments[0][0].title, clipTitle);
        compare(mediaDialog().visible, false);
    }
    function test_liveClickRequestsExactVideoWithoutDialog() {
        const tile = videoTile();
        mouseClick(tile, tile.width / 2, tile.artHeight / 2);
        assertPlaybackRequested();
    }
    function test_liveEnterRequestsExactVideoWithoutDialog() {
        const tile = videoTile();
        tile.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(tile, "activeFocus", true);
        keyClick(Qt.Key_Enter);
        assertPlaybackRequested();
    }
    function test_previewClickShowsCatalogueDialogWithoutPlayback() {
        page.artistData = preview;
        const tile = videoTile();
        mouseClick(tile, tile.width / 2, tile.artHeight / 2);
        const dialog = mediaDialog();
        tryCompare(dialog, "opened", true);
        compare(dialog.title, clipTitle);
        compare(dialog.selection.sourceUrl, watchUrl);
        compare(play.count, 0);
        dialog.close();
        tryCompare(dialog, "visible", false);
    }
}

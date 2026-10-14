import QtQuick
import QtTest
import "../../qml/vinyl/VinylArtistCatalogue.js" as Catalogue

TestCase {
    name: "VinylArtistPage"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem, name); }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        compare(host.artistData.name, "Eminem");
        host.artistData.open("Ilaiyaraaja");
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
    }
    function cleanup() {
        if (host) { host.close(); host.destroy(); }
    }
    function test_creditProjectionDoesNotMergeUnrelatedArtists() {
        var source = {albums: [
            {id:"a",title:"Album",year:2000,artists:["A.R. Rahman"],tracks:[
                {title:"Song",artists:["S. P. Balasubrahmanyam"],seconds:120}]},
            {id:"b",title:"Other",year:2001,artists:["Shankar Mahadevan"],tracks:[]}
        ]};
        compare(Catalogue.artist(source, "A. R. Rahman").releases[0].credit, "album");
        compare(Catalogue.artist(source, "S. P. Balasubrahmanyam").releases[0].credit, "track");
        compare(Catalogue.artist(source, "K. V. Mahadevan").releases.length, 0);
        compare(Catalogue.artist(source, "Kavita Subrahmanyam").songs.length, 0);
    }
    function test_worldArtistAndCreditRoutesPreservePlayback() {
        var playing = host.pageData.playingEditionId;
        var position = host.pageData.position;
        host.page = "world";
        host.openWorldItem({kind:"artists",title:"Michael Jackson"});
        compare(host.page, "artist");
        compare(host.artistData.record.name, "Michael Jackson");
        compare(host.artistData.record.releases.length, 3);
        compare(host.pageData.playingEditionId, playing);
        compare(host.pageData.position, position);
        host.artistBack();
        compare(host.page, "world");
        host.pageData.artistRequested("Ilayaraja");
        compare(host.page, "artist");
        compare(host.artistData.record.name, "Ilaiyaraaja");
    }
    function test_albumRoundTripKeepsArtistPositionAndPlayingState() {
        host.artistData.newestFirst = false;
        host.artistScroll.contentY = 100;
        var expected = host.artistData.releases[0].album.title;
        var playing = host.pageData.playingEditionId;
        item("vinylArtistAlbum_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, expected);
        compare(host.pageData.playingEditionId, playing);
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistData.newestFirst, false);
        compare(host.artistScroll.contentY, 100);
    }
    function test_nestedArtistBackRestoresOriginalAlbum() {
        host.page = "album";
        var album = host.pageData.shownEditionId;
        host.openArtist("Michael Jackson");
        item("vinylArtistAlbum_0").activate();
        item("vinylAlbumBack").activate();
        host.openArtist("Ilaiyaraaja");
        host.artistBack();
        compare(host.artistData.record.name, "Michael Jackson");
        host.artistBack();
        compare(host.page, "album");
        compare(host.pageData.shownEditionId, album);
        item("vinylAlbumBack").activate();
        compare(host.page, "world");
    }
    function test_songSelectionAndSongPageReturn() {
        var expected = host.artistData.songs[0];
        item("vinylArtistSong_0").activate();
        compare(host.page, "artist");
        compare(host.pageData.playingEditionId, expected.album.id);
        compare(host.pageData.playingTrack.title, expected.track.title);
        host.openSong();
        compare(host.songReturnPage, "artist");
        compare(host.page, "song");
        item("vinylSongArtistLink").activate();
        compare(host.page, "artist");
        host.artistBack();
        compare(host.page, "song");
        item("vinylSongBack").activate();
        compare(host.page, "artist");
    }
    function test_multipleNamesAreNotTreatedAsOneIdentity() {
        host.openArtist("Michael Jackson · Joni Mitchell");
        compare(host.artistChoices.length, 2);
        compare(host.artistData.record.name, "Ilaiyaraaja");
        tryVerify(function() { return item("vinylArtistChoice_1") !== null; });
        item("vinylArtistChoice_1").activate();
        compare(host.artistData.record.name, "Joni Mitchell");
    }
    function test_sortFilterAndSaveKeyboard() {
        var first = host.artistData.releases[0].album.year;
        var sort = item("vinylArtistSort");
        sort.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(host.artistData.newestFirst, false);
        verify(host.artistData.releases[0].album.year < first);
        var save = item("vinylArtistSave");
        save.forceActiveFocus();
        keyClick(Qt.Key_Return);
        verify(host.artistData.saved);
        host.openArtist("Michael Jackson");
        verify(!host.artistData.saved);
        host.artistBack();
        verify(host.artistData.saved);
        host.openArtist("S. P. Balasubrahmanyam");
        item("vinylArtistFilter_track").activate();
        compare(host.artistData.filter, "track");
        verify(host.artistData.releases.length > 0);
        verify(host.artistData.releases.every(function(r) { return r.credit === "track"; }));
    }
    function test_thinAndUnknownArtist() {
        host.openArtist("Joni Mitchell");
        compare(host.artistData.releases.length, 1);
        host.openArtist("Uncatalogued artist");
        compare(host.artistData.releases.length, 0);
        compare(host.artistData.songs.length, 0);
        compare(item("vinylArtistName").text, "Uncatalogued artist");
        verify(item("vinylArtistSave").visible);
    }
    function test_eminemSectionsPlaylistAndSingleRoundTrip() {
        host.artistData.open("Eminem");
        compare(host.artistData.record.name, "Eminem");
        verify(host.artistData.releases.length >= 6);
        compare(host.artistData.singles.length, 3);
        verify(host.artistData.context.history.length > 0);
        compare(host.artistData.songs[0].track.title, "Without Me");
        verify(!host.artistData.createPlaylist("   "));
        verify(host.artistData.createPlaylist("My Eminem mix"));
        compare(host.artistData.playlists.length, 2);
        compare(host.artistData.playlists[1].songs.length, 5);
        item("vinylArtistPlaylist_1").activate();
        tryVerify(function() { return item("vinylArtistPlaylistSong_0") !== null; });
        var selected = host.artistData.playlists[1].songs[0];
        mouseClick(item("vinylArtistPlaylistSong_0"));
        compare(host.pageData.playingTrack.title, selected.track.title);
        item("vinylArtistNav_4").activate();
        verify(host.artistScroll.contentY > 0);
        var oldY = host.artistScroll.contentY;
        item("vinylArtistSingle_0").activate();
        compare(host.page, "album");
        compare(host.pageData.shownEdition.name, "Somebody Save Me");
        item("vinylAlbumBack").activate();
        compare(host.page, "artist");
        compare(host.artistScroll.contentY, oldY);
        host.artistData.open("Joni Mitchell");
        compare(host.artistData.playlists.length, 1);
        host.artistData.open("Eminem");
        compare(host.artistData.playlists[1].name, "My Eminem mix");
    }
    function test_captureStates() {
        if (typeof VinylCaptureDirectory === "undefined" || !VinylCaptureDirectory)
            skip("Set VINYL_CAPTURE_DIR for visual captures");
        var cases = [
            {name:"Eminem",width:1280,height:720,y:0,tag:"artist-1280"},
            {name:"Eminem",width:1600,height:900,y:0,tag:"artist-1600"},
            {name:"Eminem",width:1280,height:720,y:0,nav:4,tag:"artist-about"},
            {name:"S. P. Balasubrahmanyam",width:1280,height:720,y:0,tag:"artist-credits"},
            {name:"Joni Mitchell",width:960,height:720,y:0,tag:"artist-thin"}
        ];
        for (var i = 0; i < cases.length; ++i) {
            var data = cases[i];
            host.close(); host.destroy();
            var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
            host = component.createObject(null, {width: data.width, height: data.height,
                flags: Qt.Window | Qt.FramelessWindowHint});
            verify(host !== null);
            host.pageData.frozen = true;
            host.artistData.open(data.name);
            tryCompare(host, "ready", true);
            tryCompare(host, "width", data.width);
            tryCompare(host, "height", data.height);
            host.artistScroll.contentY = data.y;
            if (data.nav !== undefined) item("vinylArtistNav_" + data.nav).activate();
            for (var cover = 0; cover < host.artistData.releases.length; ++cover)
                tryCompare(item("vinylArtistAlbumArt_" + cover), "ready", true);
            host.requestUpdate();
            wait(600); // Allow the shared poster primitive's 280 ms ready fade to finish.
            waitForRendering(host.contentItem);
            var captured = false;
            verify(host.contentItem.grabToImage(function(result) {
                captured = result.saveToFile(VinylCaptureDirectory + "/" + data.tag + ".png");
            }, Qt.size(data.width, data.height)));
            tryVerify(function() { return captured; });
        }
    }
}

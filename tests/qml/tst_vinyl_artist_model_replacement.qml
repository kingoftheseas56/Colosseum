import QtQuick
import QtTest

TestCase {
    name: "VinylArtistModelReplacement"
    when: windowShown
    property var host
    function cleanup() {
        if (host) { host.close(); host.destroy(); host = null; }
    }
    function test_sameCountRefreshUpdatesVideoAndActivation() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
        host.artistData.open("Eminem");
        findChild(host.contentItem, "vinylArtistTab_videos").activate();
        var playing = host.pageData.playingEditionId;
        var count = host.artistData.media.videos.length;
        verify(count > 0);
        var catalogue = Object.assign({}, host.artistData.mediaCatalogue);
        var artist = Object.assign({}, catalogue.Eminem);
        artist.videos = artist.videos.map(function(video, index) {
            return Object.assign({}, video, {
                title: "Refreshed video " + index,
                sourceUrl: "https://example.test/refreshed-video/" + index
            });
        });
        catalogue.Eminem = artist;
        host.artistData.mediaCatalogue = catalogue;
        compare(host.artistData.media.videos.length, count);
        var card = findChild(host.contentItem, "vinylArtistVideoCard_0");
        verify(card !== null);
        compare(card.record.title, "Refreshed video 0");
        compare(card.record.sourceUrl, "https://example.test/refreshed-video/0");
        card.forceActiveFocus();
        card.activate();
        var source = findChild(host, "vinylArtistMediaSource");
        tryCompare(source, "visible", true);
        compare(source.accessibleDescription, "https://example.test/refreshed-video/0");
        keyClick(Qt.Key_Escape);
        tryCompare(card, "activeFocus", true);
        compare(host.pageData.playingEditionId, playing);
    }
}

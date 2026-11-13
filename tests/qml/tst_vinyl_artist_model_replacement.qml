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
        var tab = findChild(host.contentItem, "vinylArtistTab_videos");
        verify(tab !== null);
        var artistScroll = host.artistScroll;
        var tabAt = tab.mapToItem(artistScroll.contentItem, 0, 0);
        artistScroll.contentY = Math.max(0, Math.min(
                Math.max(0, artistScroll.contentHeight - artistScroll.height), tabAt.y));
        verify(waitForRendering(tab));
        tabAt = tab.mapToItem(artistScroll, 0, 0);
        verify(tabAt.y >= 0 && tabAt.y + tab.height <= artistScroll.height,
                "The Videos tab must be inside the artist viewport before clicking");
        mouseClick(tab);
        tryCompare(host.artistData, "activeTab", "videos");
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
        tryCompare(card, "visible", true);
        card.forceActiveFocus();
        verify(card.activate());
        var source = findChild(host, "vinylArtistMediaSource");
        tryCompare(source, "visible", true);
        compare(source.accessibleDescription, "https://example.test/refreshed-video/0");
        keyClick(Qt.Key_Escape);
        tryCompare(card, "activeFocus", true);
        compare(host.pageData.playingEditionId, playing);
    }
}

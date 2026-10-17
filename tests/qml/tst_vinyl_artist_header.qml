import QtQuick
import QtTest
TestCase {
    name: "VinylArtistHeader"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem, name) || findChild(host, name) }
    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml")
        verify(component.status === Component.Ready, component.errorString())
        host = component.createObject(null, {width: 1280, height: 720})
        verify(host !== null)
        host.pageData.frozen = true
        tryCompare(host, "ready", true)
        host.artistData.reducedMotion = true
    }
    function cleanup() { if (host) { host.close(); host.destroy(); host = null } }
    function test_stickyHeaderDoesNotActivateCoveredRelease() {
        var cards = host.artistData.discographyCards
        var card = null
        for (var i = 0; i < cards.length; ++i) {
            if (!cards[i].playable || !cards[i].album || !cards[i].album.tracks.length) continue
            var candidate = item("vinylArtistDiscographyCard_" + i)
            var x = candidate.mapToItem(host.contentItem, candidate.width / 2, 0).x
            if (x > 250 && x < 900) { card = candidate; break }
        }
        verify(card !== null, "Need a playable release underneath the blank header region")
        var point = card.mapToItem(host.contentItem, card.width / 2, 30)
        host.artistScroll.contentY += point.y - 38
        waitForRendering(host.contentItem)
        point = card.mapToItem(host.contentItem, card.width / 2, 30)
        verify(point.y > 0 && point.y < 76)
        mouseClick(host.contentItem, point.x, point.y)
        compare(host.page, "artist", "Sticky destination header must swallow clicks over releases")
    }
    function test_longIdentityRespectsDestinationHeader_data() {
        return [{tag:"normal", width:1280, height:720},
                {tag:"narrow", width:640, height:480}];
    }
    function test_longIdentityRespectsDestinationHeader(data) {
        host.width = data.width; host.height = data.height;
        host.artistData.open("The Extremely Long Artist Name With Many Words And A Collective Orchestra Ensemble");
        host.artistScroll.contentY = 0;
        waitForRendering(host.contentItem);
        var title = item("vinylArtistName");
        var identity = title.parent;
        var header = item("vinylArtistTitleBar");
        var top = identity.mapToItem(host.contentItem, 0, 0).y;
        var headerBottom = header.mapToItem(host.contentItem, 0, header.height).y;
        // Allow floating-point roundoff, not a visible pixel of overlap.
        verify(top >= headerBottom + 12 - 0.001,
               "Artist eyebrow must clear Back/header: top=" + top + ", headerBottom=" + headerBottom);
        verify(identity.y + identity.height <= host.artistScroll.heroItem.height,
               "Identity remains inside hero");
    }

    function test_shortIdentityPreservesApprovedHeroSize() {
        for (var size of [[1280,720], [640,480], [1600,900]]) {
            host.width = size[0]; host.height = size[1];
            waitForRendering(host.contentItem);
            compare(host.artistScroll.heroItem.height,
                    Math.max(500, Math.min(760, Math.round(host.artistScroll.height * 0.66))));
        }
    }

}

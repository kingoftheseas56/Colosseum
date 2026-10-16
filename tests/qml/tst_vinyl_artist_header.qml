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
}

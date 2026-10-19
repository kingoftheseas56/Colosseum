// Evidence harness: real production PortraitTile and Image loader, synthetic loopback only.
import QtQuick
import QtTest
import "../qml" as UI

Item {
    id: host
    width: 400
    height: 300
    property string ownedOrigin: "http://127.0.0.1:18886"
    property string originalOrigin: "http://127.0.0.1:18887"
    Component { id: tileFactory; UI.PortraitTile { caption: "Fixture poster"; keyboardEnabled: false } }
    TestCase {
        name: "PortraitTileOwnedFallback"
        when: windowShown
        function imageIn(item) {
            if (item.sourceSize !== undefined && item.status !== undefined && item.source !== undefined)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = imageIn(item.children[i])
                if (found) return found
            }
            return null
        }
        function make(cover, candidates) {
            var tile = tileFactory.createObject(host, {cover:cover, coverCandidates:candidates})
            verify(tile !== null)
            verify(imageIn(tile) !== null)
            return tile
        }
        function receipt(kind, tile, art) {
            console.log("PORTRAIT_RECEIPT " + JSON.stringify({kind:kind, source:String(art.source), status:art.status,
                posterIndex:tile.posterIndex, sources:tile.posterSources}))
        }
        function test_1_owned_success_never_tries_original() {
            var cover=host.ownedOrigin+"/success.webp", original=host.originalOrigin+"/success-original.webp"
            var tile=make(cover,[cover,original,original]), art=imageIn(tile)
            try {
                compare(tile.posterSources.length,2)
                compare(tile.posterSources[0],cover)
                tryCompare(art,"status",Image.Ready,5000)
                wait(400)
                compare(tile.posterIndex,0)
                compare(String(art.source),cover)
                receipt("owned-success",tile,art)
            } finally { tile.destroy(); wait(50) }
        }
        function test_2_owned_error_tries_original_once() {
            var cover=host.ownedOrigin+"/missing.webp", original=host.originalOrigin+"/fallback.webp"
            var tile=make(cover,[cover,original,cover,original]), art=imageIn(tile)
            try {
                compare(tile.posterSources.length,2)
                tryCompare(art,"status",Image.Ready,5000)
                wait(400)
                compare(tile.posterIndex,1)
                compare(String(art.source),original)
                receipt("original-fallback",tile,art)
            } finally { tile.destroy(); wait(50) }
        }
        function test_3_exhaustion_does_not_retry_same_url() {
            var cover=host.ownedOrigin+"/exhausted.webp", original=host.originalOrigin+"/exhausted.webp"
            var tile=make(cover,[cover,original,original,cover]), art=imageIn(tile)
            try {
                compare(tile.posterSources.length,2)
                tryCompare(tile,"posterIndex",1,5000)
                tryCompare(art,"status",Image.Error,5000)
                wait(800)
                compare(tile.posterIndex,1)
                compare(String(art.source),original)
                receipt("exhausted-no-retry",tile,art)
            } finally { tile.destroy(); wait(50) }
        }
    }
}

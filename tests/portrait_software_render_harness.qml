// Actual painted pixels, not Image.Ready or item geometry. Run with software backend.
import QtQuick
import QtTest
import "../qml" as UI
Item {
    id: host
    width: 400; height: 300
    Component { id: factory; UI.PortraitTile { x: 20; y: 20; posterWidth: 140; keyboardEnabled: false } }
    TestCase {
        name: "PortraitSoftwarePixels"
        when: windowShown
        function initTestCase() {
            if (host.GraphicsInfo.api !== GraphicsInfo.Software)
                skip("This regression requires QT_QUICK_BACKEND=software")
        }
        function imageIn(item) {
            if (item.sourceSize !== undefined && item.status !== undefined && item.source !== undefined) return item
            for (var i = 0; i < item.children.length; i++) {
                var result = imageIn(item.children[i]); if (result) return result
            }
            return null
        }
        function make(properties) {
            var tile = createTemporaryObject(factory, host, properties)
            verify(tile !== null)
            return tile
        }
        function test_cover_is_actually_painted() {
            var tile = make({cover: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAADCAIAAAA2iEnWAAAAEElEQVR4nGMQ3PsaiBhQKABngApXnOpbqgAAAABJRU5ErkJggg=="}), art = imageIn(tile)
            verify(art !== null)
            tryCompare(art, "status", Image.Ready, 3000)
            tryVerify(function() { return art.opacity >= 0.99 }, 2000)
            var frame = grabImage(tile)
            compare(frame.red(70,70),17); compare(frame.green(70,70),189); compare(frame.blue(70,70),235)
        }
        function test_placeholder_and_progress_are_painted() {
            var tile = make({cover:"", c1:"#1f405b", c2:"#1f405b", progress:0.5})
            wait(50)
            var frame = grabImage(tile)
            compare(frame.red(70,70),31); compare(frame.green(70,70),64); compare(frame.blue(70,70),91)
            compare(frame.red(20,tile.height-2),240); compare(frame.green(20,tile.height-2),196); compare(frame.blue(20,tile.height-2),74)
        }
        function test_caption_update_changes_pixels_only_below_art() {
            var tile = make({cover:"", c1:"#1f405b", c2:"#1f405b", caption:""})
            wait(50)
            var before = grabImage(tile)
            tile.caption = "CAPTION X"
            wait(80)
            var after = grabImage(tile), changed = 0
            for (var y = tile.height-50; y < tile.height-5; y++)
                for (var x = 9; x < tile.width-9; x++)
                    if (before.pixel(x,y) !== after.pixel(x,y)) changed++
            verify(changed > 20, "Caption must actually paint readable pixels")
            compare(before.pixel(70,70), after.pixel(70,70))
        }
    }
}

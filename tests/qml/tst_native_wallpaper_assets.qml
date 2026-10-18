import QtQuick
import QtTest
import "../../qml/update" as Updates
import "../../qml/vinyl" as Vinyl

TestCase {
    id: testCase
    name: "NativeWallpaperAssets"
    when: windowShown
    width: 960; height: 540
    property var view: null
    readonly property string redImage: "data:image/svg+xml," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='red'/></svg>")
    readonly property string blueImage: "data:image/svg+xml," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='blue'/></svg>")

    QtObject {
        id: content
        property int generation: 1
        property var assets: ({})
        function assetUrl(relative) { return assets[relative] || "" }
    }
    Component {
        id: gallery
        Updates.UpdateLivingGallery {
            width: 960; height: 540
            contentStore: content
        }
    }
    Component {
        id: backdrop
        Vinyl.VinylBackdrop {
            width: 960; height: 540
            contentStore: content
        }
    }
    function init() {
        content.assets = ({"assets/wallpaper/captured-motion.jpg":redImage,
                           "assets/wallpaper/cold-ripple.jpg":redImage})
    }
    function cleanup() {
        if (view) { view.destroy(); view = null }
    }
    function test_gallery_uses_content_owner_and_refreshes_on_publication_change() {
        view = gallery.createObject(testCase)
        verify(view !== null)
        compare(view.fallbackSource, redImage)
        content.assets["assets/wallpaper/captured-motion.jpg"] = blueImage
        compare(view.fallbackSource, redImage)
        content.generation++
        tryCompare(view, "fallbackSource", blueImage)
    }
    function test_vinyl_uses_the_same_content_owner() {
        view = backdrop.createObject(testCase)
        verify(view !== null)
        compare(view.fallbackSource, redImage)
        content.assets["assets/wallpaper/cold-ripple.jpg"] = blueImage
        content.generation++
        tryCompare(view, "fallbackSource", blueImage)
    }
}

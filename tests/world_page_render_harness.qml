import QtQuick
import QtTest
import "../qml" as UI

Item {
    id: scene
    width: 1280
    height: 720

    UI.WorldPage {
        id: world
        anchors.fill: parent
        backdrop: null
        medium: "Tankoban"

        Rectangle {
            width: parent.width
            height: 2000
            color: "#ee2244"
        }
    }

    UI.RoundedPosterImage {
        id: poster
        width: 60
        height: 90
        revealDuration: 0
        sources: ["data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGMQPBP6HwAEVgIyMEB53wAAAABJRU5ErkJggg=="]
    }

    TestCase {
        name: "WorldPageRendering"
        when: windowShown

        function initTestCase() {
            console.info("WORLD_PAGE_GRAPHICS_API", world.GraphicsInfo.api,
                         "SOFTWARE_API", GraphicsInfo.Software)
        }

        function init() {
            world.pageFlickable.contentY = 0
            wait(100)
        }

        function test_unscrolled_content_is_visible() {
            var image = grabImage(world)
            compare(image.pixel(600, 300), "#ee2244")
        }

        function test_loaded_poster_is_visible() {
            tryCompare(poster, "ready", true)
            wait(100)
            var image = grabImage(poster)
            compare(image.pixel(30, 45), "#11cc55")
        }

        function test_scrolled_content_remains_visible_data() {
            return [
                { tag: "tankoban", medium: "Tankoban" },
                { tag: "theatre", medium: "Theatre" }
            ]
        }

        function test_scrolled_content_remains_visible(data) {
            world.medium = data.medium
            world.pageFlickable.contentY = 200
            wait(200)
            compare(world.pageFlickable.contentY, 200)
            compare(world.pageFlickable.layer.enabled,
                    world.pageFlickable.GraphicsInfo.api !== GraphicsInfo.Software)
            var image = grabImage(world)
            compare(image.pixel(600, 300), "#ee2244")
        }
    }
}

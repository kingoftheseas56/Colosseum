import QtQuick
import QtTest
import "../qml" as UI

Item {
    width: 1280
    height: 720

    UI.TankobanComicsTab {
        id: tab
        width: 1100
        viewportHeight: 720
    }

    TestCase {
        name: "ComicsShelfResidency"
        when: windowShown

        function rows(title) {
            return [{ caption: title, title: title, cover: "", locgId: "fixture" }]
        }
        function shelf(label) {
            var children = tab.children
            for (var i = 0; i < children.length; i++) {
                var child = children[i]
                if (child.modelData && child.modelData.label === label && child.built !== undefined)
                    return child
            }
            return null
        }
        function test_peer_response_retains_existing_shelf_and_view() {
            tab.comicShelves = [
                { label: "Marvel", kind: "publisher", arg: "Marvel", rows: rows("First") },
                { label: "DC", kind: "publisher", arg: "DC", rows: [] }
            ]
            wait(150)
            var original = shelf("Marvel")
            verify(original !== null)
            compare(shelf("DC").built, false, "An empty pending slot must not construct a poster shelf")
            tab.comicShelves = [
                { label: "Marvel", kind: "publisher", arg: "Marvel", rows: rows("First") },
                { label: "DC", kind: "publisher", arg: "DC", rows: rows("Second") }
            ]
            wait(150)
            compare(shelf("Marvel"), original, "A peer response must not destroy the populated shelf")
            verify(shelf("DC").visible)
        }
    }
}

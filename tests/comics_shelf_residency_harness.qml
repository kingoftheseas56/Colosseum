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
        function init() {
            tab.comicShelves = []
            tab.viewportTop = 0
            tab.viewportHeight = 720
            wait(30)
        }
        function populatedShelves() {
            var result = []
            for (var i = 0; i < 15; i++)
                result.push({label: "Shelf " + i, rows: rows("Title " + i)})
            return result
        }
        function verifyViewportResidency() {
            wait(150)
            var first = shelf("Shelf 0")
            var last = shelf("Shelf 14")
            verify(first !== null && last !== null)
            verify(first.built, "The first nearby shelf must load")
            verify(!last.near, "The final shelf must be outside the activation range")
            compare(last.built, false, "An offscreen shelf must not load against its initial y=0")
            tab.viewportTop = last.y
            tryCompare(last, "built", true)
            compare(first.built, true, "A previously visited shelf must retain its view")
        }
        function test_initial_population_waits_for_column_layout() {
            tab.comicShelves = populatedShelves()
            verifyViewportResidency()
        }
        function test_pending_slots_populate_without_building_far_shelves() {
            var pending = populatedShelves()
            for (var i = 0; i < pending.length; i++) pending[i].rows = []
            tab.comicShelves = pending
            wait(30)
            tab.comicShelves = populatedShelves()
            verifyViewportResidency()
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

// Arc 74-C: actual shared ContinueTile pointer/keyboard semantics.
// This is a QML runtime test with controlled records, no personal state writes.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: test
    name: "Arc74ContinueTile"
    when: windowShown

    Window { id: window; width: 800; height: 570; visible: true }
    Component { id: tileComponent; App.ContinueTile {} }

    property var tile: null
    property int resumed: 0
    property int detailed: 0
    property int removed: 0

    function spawn(kind, chapter, variant) {
        var entry = { id: kind === "tankoban" ? "mal:1" : "mal:13",
            kind: kind, title: "Fixture series", sub: "Chapter 47",
            cover: Qt.resolvedUrl("../../assets/icons/manga.svg").toString(), progress: .4, resume: chapter ? { chapterId: chapter, page: 12 } : {} }
        tile = tileComponent.createObject(window.contentItem, {
            x: 35, y: 35, variant: variant || "world", entry: entry
        })
        verify(tile !== null)
        tile.resumeRequested.connect(function() { resumed++ })
        tile.detailRequested.connect(function() { detailed++ })
        tile.removeRequested.connect(function() { removed++ })
        wait(45)
    }

    function init() { resumed = 0; detailed = 0; removed = 0 }
    function cleanup() { if (tile) tile.destroy(); tile = null }

    function test_manga_poster_resumes_and_details_is_explicit() {
        spawn("manga", "tankoyomi:pt:fixture:chapter-47")
        verify(tile.hasMangaResume)
        var details = findItem(tile, "continueMangaDetailsButton")
        verify(details !== null && details.visible, "Details must be independently visible")
        mouseClick(tile, 12, 60, Qt.LeftButton)
        compare(resumed, 1)
        compare(detailed, 0)
        mouseClick(tile, 22, 20, Qt.LeftButton)
        compare(resumed, 1)
        compare(detailed, 1)
        compare(removed, 0)
    }

    function test_manga_center_also_resumes() {
        spawn("manga", "tankoyomi:pt:fixture:chapter-47")
        mouseClick(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
        compare(resumed, 1)
        compare(detailed, 0)
    }

    function test_volume_poster_and_center_resume() {
        spawn("tankoban", "volume:mal:1:5")
        mouseClick(tile, 12, 65, Qt.LeftButton)
        mouseClick(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
        compare(resumed, 2)
        compare(detailed, 0)
    }

    function test_manga_without_saved_target_goes_to_details() {
        spawn("manga", "")
        verify(!tile.hasMangaResume)
        mouseClick(tile, 14, 59, Qt.LeftButton)
        mouseClick(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
        compare(resumed, 0)
        compare(detailed, 2)
        tile.activateKeyboardContext(1)
        compare(resumed, 0)
        compare(detailed, 3)
    }

    function test_non_manga_semantics_unchanged() {
        for (var kind of ["video", "book", "comic"]) {
            spawn(kind, "chapter-47")
            verify(!tile.hasMangaResume)
            mouseClick(tile, 12, 61, Qt.LeftButton)
            mouseClick(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
            compare(detailed, 1)
            compare(resumed, 1)
            tile.destroy(); tile = null
            resumed = 0; detailed = 0
        }
    }

    function test_home_variant_manga_resumes() {
        spawn("manga", "qualified:47", "home")
        mouseClick(tile, 170, 70, Qt.LeftButton)
        compare(resumed, 1)
        mouseClick(tile, 15, 18, Qt.LeftButton)
        compare(detailed, 1)
    }

    function test_primary_and_context_dispatch() {
        spawn("manga", "qualified:47")
        tile.activatePrimary()
        compare(resumed, 1)
        tile.activateKeyboardContext(0)
        compare(detailed, 1)
        tile.activateKeyboardContext(1)
        compare(resumed, 2)
        tile.activateKeyboardContext(2)
        compare(removed, 1)
    }

    function findItem(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var child = findItem(children[i], name)
            if (child) return child
        }
        return null
    }
}

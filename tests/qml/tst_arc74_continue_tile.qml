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

    function test_shared_pointer_contract_data() {
        var rows = []
        for (var kind of ["manga", "tankoban", "video", "book", "comic", "music"])
            for (var variant of ["home", "world"])
                rows.push({tag: kind + "_" + variant, kind: kind, variant: variant})
        return rows
    }
    function test_shared_pointer_contract(data) {
        spawn(data.kind, "qualified:47", data.variant)
        compare(findItem(tile, "continueMangaDetailsButton"), null)
        mouseClick(tile, data.variant === "home" ? 170 : 12, 60, Qt.LeftButton)
        compare(detailed, 1); compare(resumed, 0)
        mouseClick(tile, data.variant === "home" ? 56 : tile.width / 2, tile.height / 2, Qt.LeftButton)
        compare(detailed, 1); compare(resumed, 1)
        compare(removed, 0)
    }

    function test_missing_saved_target_still_dispatches_to_resume_owner() {
        spawn("manga", "")
        mouseClick(tile, 14, 59, Qt.LeftButton)
        compare(detailed, 1); compare(resumed, 0)
        mouseClick(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
        compare(detailed, 1); compare(resumed, 1)
        tile.activateKeyboardContext(1)
        compare(detailed, 1); compare(resumed, 2)
    }

    function test_primary_and_context_dispatch() {
        spawn("manga", "qualified:47")
        tile.activatePrimary()
        compare(detailed, 1); compare(resumed, 0)
        tile.activateKeyboardContext(0)
        compare(detailed, 2)
        tile.activateKeyboardContext(1)
        compare(resumed, 1)
        tile.activateKeyboardContext(2)
        compare(removed, 1)
    }

    function test_standalone_keyboard_details_and_resume() {
        spawn("manga", "qualified:47")
        window.requestActivate()
        var details = findItem(tile, "continueDetail_mal:13")
        verify(details !== null)
        details.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return details.activeFocus })
        keyClick(Qt.Key_Return)
        compare(detailed, 1); compare(resumed, 0)
        var resume = findItem(tile, "continueResume_mal:13")
        verify(resume !== null)
        resume.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        compare(detailed, 1); compare(resumed, 1)
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

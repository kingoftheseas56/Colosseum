import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault Slice 16 — an unavailable root keeps its shelf tile in place and paints a gray, explicit
// state. This is a seedable production tile test; no filesystem or backend is needed.
TestCase {
    name: "VaultHome"
    when: windowShown

    Window { id: testWindow; width: 480; height: 360; visible: true }
    Component { id: tileComp; Colosseum.VaultTile {} }
    property var tile: null

    function init() {
        tile = tileComp.createObject(testWindow, {
            modelData: { key: "series-1", title: "Series", kind: "comic", count: 2,
                         awayCount: 2, errorCount: 0, coverUrl: "" }
        })
        verify(tile !== null)
        wait(40)
    }

    function cleanup() {
        if (tile) tile.destroy()
        tile = null
    }

    function test_away_tile_stays_present_and_gray() {
        verify(tile.away)
        compare(tile.objectName, "vaultTile_series-1")
        compare(findChild(tile, "vaultTileCover").opacity, 0.48)
        verify(findText(tile, "Unavailable") !== null)
    }

    function test_returned_tile_revives_without_changing_identity() {
        tile.modelData = { key: "series-1", title: "Series", kind: "comic", count: 2,
                           awayCount: 0, errorCount: 0, coverUrl: "" }
        wait(20)
        verify(!tile.away)
        compare(tile.objectName, "vaultTile_series-1")
        compare(findChild(tile, "vaultTileCover").opacity, 1.0)
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
    }

    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}

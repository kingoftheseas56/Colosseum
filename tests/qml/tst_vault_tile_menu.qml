import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault Slice 17: every tile exposes the reversible identity actions and paints only a seeded
// identity decoration. The test is intentionally production-component-only; VaultPage wires the
// signals to the native façade in the next red/green slice.
TestCase {
    name: "VaultTileMenu"
    when: windowShown

    Window { id: testWindow; width: 560; height: 420; visible: true }
    Component { id: tileComp; Colosseum.VaultTile {} }
    property var tile: null
    SignalSpy { id: openSpy; signalName: "openRequested" }
    SignalSpy { id: revealSpy; signalName: "revealRequested" }
    SignalSpy { id: identifySpy; signalName: "identifyRequested" }
    SignalSpy { id: unidentifySpy; signalName: "unidentifyRequested" }
    SignalSpy { id: reshelveSpy; signalName: "reshelveRequested" }
    SignalSpy { id: hideSpy; signalName: "hideRequested" }

    function init() {
        tile = tileComp.createObject(testWindow, {
            modelData: {
                key: "series-1", title: "Cowboy Bebop", kind: "comic", count: 3,
                awayCount: 0, errorCount: 0, coverUrl: "",
                identSource: "MAL", synopsis: "A seeded synopsis.",
                synopsisSource: "MAL", identityId: "mal:1"
            }
        })
        verify(tile !== null)
        openSpy.target = tile
        revealSpy.target = tile
        identifySpy.target = tile
        unidentifySpy.target = tile
        reshelveSpy.target = tile
        hideSpy.target = tile
        wait(40)
    }

    function cleanup() {
        openSpy.target = null
        revealSpy.target = null
        identifySpy.target = null
        unidentifySpy.target = null
        reshelveSpy.target = null
        hideSpy.target = null
        if (tile) tile.destroy()
        tile = null
    }

    function test_seeded_identity_badge_remains_exposed_without_shelf_synopsis() {
        compare(tile.identSource, "MAL")
        verify(findText(tile, "MAL") !== null)
        verify(findText(tile, "A seeded synopsis.") === null)
        verify(findText(tile, "Source: MAL") === null)
    }

    function test_synopsis_does_not_change_tile_height_or_render_on_shelf() {
        compare(tile.height, 235)
        verify(findChild(tile, "vaultTileSynopsis") === null)
        verify(findChild(tile, "vaultTileSynopsisSource") === null)
    }

    function test_menu_renders_all_six_actions_and_emits_them() {
        var spies = [openSpy, revealSpy, identifySpy, unidentifySpy, reshelveSpy, hideSpy]

        var menuButton = findChild(tile, "vaultTileMenu")
        verify(menuButton !== null)
        mouseClick(menuButton)
        wait(20)

        var labels = ["Open", "Reveal in Explorer", "Identify…", "Un-identify", "This is a book…", "Hide"]
        for (var i = 0; i < labels.length; i++)
            verify(findText(tile, labels[i]) !== null, labels[i] + " menu item is missing")

        var ids = ["vaultTileOpen", "vaultTileReveal", "vaultTileIdentify", "vaultTileUnidentify",
                   "vaultTileReshelveBook", "vaultTileHide"]
        for (var j = 0; j < ids.length; j++) {
            var action = findChild(tile, ids[j])
            verify(action !== null, ids[j] + " action is missing")
            mouseClick(action)
            wait(10)
            compare(spies[j].count, 1)
            spies[j].clear()
            mouseClick(menuButton)
            wait(10)
        }
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

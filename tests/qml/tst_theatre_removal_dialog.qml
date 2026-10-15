import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "TheatreRemovalDialog"
    when: windowShown

    QtObject { id: state; property bool linkedAccount: false }
    QtObject {
        id: actions
        property int calls: 0
        property bool lastAlsoStremio: false
        function removeTheatreItem(id, type, alsoStremio) {
            calls++
            lastAlsoStremio = alsoStremio
            return true
        }
    }
    Window {
        id: window
        width: 800; height: 600; visible: true
        Colosseum.TheatreRemovalDialog {
            id: dialog
            anchors.fill: parent
            shown: true
            entry: ({ "id": "tt1", "type": "movie", "title": "Film" })
            syncState: state
            actions: actions
            onCloseRequested: shown = false
        }
    }
    function findChild(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found) return found
        }
        return null
    }
    function init() {
        actions.calls = 0
        state.linkedAccount = false
        dialog.shown = true
        wait(0)
    }
    function test_unconnected_keeps_local_removal_available() {
        var local = findChild(dialog, "removeFromColosseumAction")
        var dual = findChild(dialog, "removeFromColosseumAndStremioAction")
        verify(local !== null)
        verify(dual !== null)
        compare(local.visible, true)
        compare(dual.visible, false)
        local.triggered()
        compare(actions.calls, 1)
        compare(actions.lastAlsoStremio, false)
    }
    function test_linked_account_offers_explicit_dual_removal() {
        state.linkedAccount = true
        wait(0)
        var dual = findChild(dialog, "removeFromColosseumAndStremioAction")
        compare(dual.visible, true)
        dual.triggered()
        compare(actions.calls, 1)
        compare(actions.lastAlsoStremio, true)
    }
}

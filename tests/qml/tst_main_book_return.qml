import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "MainBookReturn"
    when: windowShown

    Window {
        id: host
        width: 960
        height: 640
        visible: true
        Colosseum.Main { id: shell; visible: false }
    }

    function test_real_layer_aliases_block_background_restore() {
        verify(shell !== null)
        shell.bookLayerRef.active = true
        verify(shell._bookReturnCovered())
        shell.bookLayerRef.opacity = 0
        verify(!shell._bookReturnCovered())
        shell.bookLayerRef.opacity = 1
        shell.bookLayerRef.visible = false
        verify(!shell._bookReturnCovered())
        shell.bookLayerRef.visible = true
        var background = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 80; height: 40; visible: true; focus: false; property string stableId: "covered-background" }',
            shell.contentItem)
        var beforeFocus = shell.activeFocusItem
        var coveredSnapshot = { item: background, identity: "covered-background", owner: null,
                                index: -1, scrolls: [] }
        verify(!shell._restoreBookReturn(coveredSnapshot, shell.bookRouteGeneration))
        compare(shell.activeFocusItem, beforeFocus)
        background.destroy()
        shell.bookLayerRef.active = false
        verify(!shell._bookReturnCovered())
    }

    function test_real_restore_preserves_offset_then_reveals_changed_identity() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Flickable { width: 220; height: 120; contentWidth: 500; contentHeight: 120; clip: true;'
            + 'property bool keyboardReturnOwner: true; property var rows: [ {id:"book-1"} ]; property var keyboardItems: rows; property int currentIndex: 0; property int revealCalls: 0;'
            + 'function keyboardIdentityForIndex(i) { return i >= 0 && i < rows.length ? rows[i].id : "" }'
            + 'function keyboardIndexForIdentity(id) { for (var i=0;i<rows.length;++i) if (rows[i].id === id) return i; return -1 }'
            + 'function itemAtIndex(i) { return target } function keyboardRevealIndex(i) { revealCalls += 1; contentX = Math.max(0, Math.min(contentWidth-width, target.x - 30)); return true }'
            + 'Item { id: target; objectName: "returnTarget"; x: 60; y: 20; width: 60; height: 60; focus: true } }', shell.contentItem)
        fixture.contentX = 110
        host.requestActivate()
        shell.visible = true
        shell.requestActivate()
        fixture.itemAtIndex(0).forceActiveFocus(Qt.OtherFocusReason)
        var snapshot = { item: fixture.itemAtIndex(0), identity: "book-1", owner: fixture,
                         index: 0, scrolls: [ { flick: fixture, x: 40, y: 0 } ] }
        var restored = shell._restoreBookReturn(snapshot, shell.bookRouteGeneration)
        verify(restored)
        compare(fixture.contentX, 40)
        compare(fixture.revealCalls, 0)

        fixture.rows = [ {id: "other"}, {id: "book-1"} ]
        fixture.itemAtIndex(0).x = 320
        fixture.contentX = 40
        fixture.revealCalls = 0
        verify(shell._restoreBookReturn(snapshot, shell.bookRouteGeneration))
        compare(fixture.currentIndex, 1)
        compare(fixture.revealCalls, 1)
        verify(fixture.contentX > 40)

        fixture.rows = [ {id: "other"} ]
        fixture.itemAtIndex(0).x = 30
        fixture.contentWidth = 220
        fixture.contentX = 100
        fixture.revealCalls = 0
        snapshot.identity = "removed-book"
        snapshot.index = 0
        snapshot.scrolls[0].x = 100
        verify(shell._restoreBookReturn(snapshot, shell.bookRouteGeneration))
        compare(fixture.currentIndex, 0)
        verify(fixture.contentX <= fixture.contentWidth - fixture.width)
        compare(fixture.revealCalls, 0)
        fixture.destroy()
    }
}

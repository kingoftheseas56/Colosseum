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

    function test_unrealized_owner_requests_reveal_before_focus() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Flickable { width: 220; height: 120; contentWidth: 500; contentHeight: 120; clip: true;'
            + 'property var rows: [ {id:"book-1"} ]; property var keyboardItems: rows; property int currentIndex: 0; property int revealCalls: 0; property bool realized: false;'
            + 'function keyboardIdentityForIndex(i) { return i >= 0 && i < rows.length ? rows[i].id : "" }'
            + 'function keyboardIndexForIdentity(id) { return id === "book-1" ? 0 : -1 }'
            + 'function keyboardItemAtIndex(i) { return realized ? target : null }'
            + 'function keyboardRevealIndex(i) { revealCalls += 1; realized = true; return true }'
            + 'Item { id: target; objectName: "virtualizedReturnTarget"; x: 60; y: 20; width: 60; height: 60; focus: false } }', shell.contentItem)
        var snapshot = { item: fixture, identity: "book-1", owner: fixture,
                         index: 0, scrolls: [ { flick: fixture, x: 0, y: 0 } ] }
        verify(shell._restoreBookReturn(snapshot, shell.bookRouteGeneration))
        compare(fixture.revealCalls, 1)
        verify(fixture.keyboardItemAtIndex(0).activeFocus)
        fixture.destroy()
    }

    function test_return_target_rejects_zero_opacity_ancestry() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 220; height: 120; opacity: 0;'
            + 'property alias target: target; Item { id: target; width: 80; height: 40; visible: true; enabled: true; focus: false } }',
            shell.contentItem)
        verify(!shell._bookReturnTargetVisible(fixture.target))
        fixture.destroy()
    }

    function test_return_target_rejects_mostly_clipped_center_inside() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 220; height: 160;'
            + 'Item { id: clipHost; x: 10; y: 10; width: 180; height: 100; clip: true;'
            + 'property alias target: target; Item { id: target; x: 20; y: 60; width: 120; height: 80; visible: true; enabled: true; focus: false } } }',
            shell.contentItem)
        verify(!shell._bookReturnTargetVisible(fixture.children[0].target))
        fixture.destroy()
    }

    function test_return_oversized_target_keeps_normal_axis_fully_contained() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 220; height: 180;'
            + 'Item { id: clipHost; width: 100; height: 100; clip: true;'
            + 'property alias target: target; Item { id: target; x: 90; y: 20; width: 40; height: 160; visible: true; enabled: true; focus: false } } }',
            shell.contentItem)
        verify(!shell._bookReturnTargetVisible(fixture.children[0].target))
        fixture.destroy()
    }

    function test_return_oversized_target_rejects_one_pixel_identifiable_overlap() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 220; height: 180;'
            + 'Item { id: clipHost; width: 100; height: 100; clip: true;'
            + 'property alias target: target; Item { id: target; x: 99; y: 20; width: 200; height: 40; visible: true; enabled: true; focus: false } } }',
            shell.contentItem)
        verify(!shell._bookReturnTargetVisible(fixture.children[0].target))
        fixture.destroy()
    }

    function test_return_oversized_target_accepts_twenty_five_percent_identifiable_portion() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 220; height: 180;'
            + 'Item { id: clipHost; width: 100; height: 100; clip: true;'
            + 'property alias target: target; Item { id: target; x: 0; y: 20; width: 200; height: 40; visible: true; enabled: true; focus: false } } }',
            shell.contentItem)
        verify(shell._bookReturnTargetVisible(fixture.children[0].target))
        fixture.destroy()
    }
}

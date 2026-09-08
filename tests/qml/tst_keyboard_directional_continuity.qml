import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum
import "../../qml/KeyboardViewport.js" as Viewport

TestCase {
    id: testCase
    name: "KeyboardDirectionalContinuity"
    when: windowShown

    Window {
        id: testWindow
        width: 640
        height: 360
        visible: true

        Item {
            id: collection
            objectName: "raggedCollection"
            width: 640
            height: 220
            focus: true
            property var entries: [
                { id: "A0", section: "A" },
                { id: "A1", section: "A" },
                { id: "A2", section: "A" },
                { id: "A3", section: "A" },
                { id: "B0", section: "B" },
                { id: "B1", section: "B" },
                { id: "B2", section: "B" }
            ]
            property var rowLengths: [4, 3]
            property int modelRevision: 1
            property int currentIndex: 0
            readonly property int count: entries.length
            readonly property string selectedId:
                currentIndex >= 0 && currentIndex < entries.length
                    ? entries[currentIndex].id : ""

            function identityForIndex(index) {
                return index >= 0 && index < entries.length ? entries[index].id : ""
            }

            function indexForIdentity(identity) {
                for (var i = 0; i < entries.length; ++i) {
                    if (entries[i].id === identity)
                        return i
                }
                return -1
            }

            function positionViewAtIndex(index, mode) {
                // The controller must use the native positioning seam when present.
                positionCalls++
            }

            property int positionCalls: 0
            Keys.onPressed: function(event) {
                if (collectionNav.handle(event))
                    event.accepted = true
            }

            Repeater {
                model: collection.entries
                delegate: Rectangle {
                    width: 120
                    height: 42
                    x: (index % 4) * 140
                    y: index < 4 ? 16 : 92
                    color: index === collection.currentIndex ? "#46a6ff" : "#253043"
                    property string stableId: modelData.id
                }
            }
        }

        Colosseum.KeyboardCollectionController {
            id: collectionNav
            view: collection
            orientation: "grid"
            columns: 4
            count: collection.count
            currentIndex: collection.currentIndex
            rowLengths: collection.rowLengths
            modelRevision: collection.modelRevision
            identityForIndex: collection.identityForIndex
            indexForIdentity: collection.indexForIdentity
        }

        GridView {
            id: nativeGrid
            objectName: "virtualizedGrid"
            y: 250
            width: 400
            height: 90
            cellWidth: 100
            cellHeight: 42
            model: 100
            focus: false
            delegate: Rectangle {
                width: 92
                height: 38
                color: GridView.isCurrentItem ? "#46a6ff" : "#253043"
            }
            Keys.onPressed: function(event) {
                if (nativeGridNav.handle(event))
                    event.accepted = true
            }
        }

        Colosseum.KeyboardCollectionController {
            id: nativeGridNav
            view: nativeGrid
            orientation: "grid"
            columns: 4
            count: nativeGrid.count
            currentIndex: nativeGrid.currentIndex
        }
    }

    Component {
        id: returnRegionComp
        Colosseum.KeyboardRegion {
            id: returnRegion
            width: 620
            height: 220
            entryItem: fallbackAction
            trapTab: true
            property alias scroll: returnScroll
            property alias sourceAction: sourceAction
            property alias overlayAction: overlayAction
            property alias fallbackAction: fallbackAction

            Flickable {
                id: returnScroll
                objectName: "returnScroll"
                width: 360
                height: 220
                contentWidth: width
                contentHeight: 720
                clip: true

                Colosseum.KeyboardAction {
                    id: sourceAction
                    objectName: "returnSource"
                    property string stableId: "source-item"
                    x: 20
                    y: 180
                    width: 180
                    height: 44
                    pointerEnabled: false
                }
            }

            Colosseum.KeyboardAction {
                id: overlayAction
                objectName: "returnOverlay"
                property string stableId: "overlay-item"
                x: 390
                y: 20
                width: 180
                height: 44
                pointerEnabled: false
            }

            Colosseum.KeyboardAction {
                id: fallbackAction
                objectName: "returnFallback"
                property string stableId: "fallback-item"
                x: 390
                y: 80
                width: 180
                height: 44
                pointerEnabled: false
            }
        }
    }

    property var returnRegion: null

    function init() {
        testWindow.requestActivate()
        collection.entries = [
            { id: "A0", section: "A" },
            { id: "A1", section: "A" },
            { id: "A2", section: "A" },
            { id: "A3", section: "A" },
            { id: "B0", section: "B" },
            { id: "B1", section: "B" },
            { id: "B2", section: "B" }
        ]
        collection.rowLengths = [4, 3]
        collection.modelRevision = 1
        collection.currentIndex = 3
        collection.positionCalls = 0
        collection.forceActiveFocus(Qt.OtherFocusReason)
        wait(20)
    }

    function cleanup() {
        if (returnRegion) {
            returnRegion.destroy()
            returnRegion = null
        }
    }

    function createReturnRegion() {
        returnRegion = returnRegionComp.createObject(testWindow)
        verify(returnRegion !== null)
        returnRegion.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        return returnRegion
    }

    function test_ragged_round_trip_preserves_stable_poster() {
        keyClick(Qt.Key_Down)
        compare(collection.currentIndex, 6)
        compare(collection.selectedId, "B2")
        keyClick(Qt.Key_Up)
        compare(collection.currentIndex, 3)
        compare(collection.selectedId, "A3")
        verify(collection.positionCalls >= 2)
    }

    function test_lateral_move_resets_lane_and_never_wraps() {
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Left)
        compare(collection.currentIndex, 5)
        keyClick(Qt.Key_Up)
        compare(collection.currentIndex, 1)
        compare(collection.selectedId, "A1")

        collection.currentIndex = 0
        keyClick(Qt.Key_Left)
        compare(collection.currentIndex, 0)
        collection.currentIndex = 3
        keyClick(Qt.Key_Right)
        compare(collection.currentIndex, 3)
    }

    function test_default_grid_preserves_geometric_lane_without_identity_callbacks() {
        var view = Qt.createQmlObject(
            'import QtQuick 2.15; Item { property int currentIndex: 3; property int count: 7;'
            + 'function forceActiveFocus(r) {} function positionViewAtIndex(i,m) {} }', testWindow.contentItem)
        var controller = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; C.KeyboardCollectionController {'
            + 'view: parent; orientation: "grid"; columns: 4 }', view)
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        var up = { key: Qt.Key_Up, modifiers: Qt.NoModifier, accepted: false }
        verify(controller.handle(down))
        compare(view.currentIndex, 6)
        verify(controller.handle(up))
        compare(view.currentIndex, 3)
        controller.destroy()
        view.destroy()
    }

    function test_lane_intent_survives_multiple_short_sections() {
        collection.entries = [
            { id: "A0" }, { id: "A1" }, { id: "A2" }, { id: "A3" },
            { id: "B0" }, { id: "B1" }, { id: "B2" },
            { id: "C0" }, { id: "C1" }, { id: "C2" }, { id: "C3" }
        ]
        collection.rowLengths = [4, 3, 4]
        collection.modelRevision = 7
        collection.currentIndex = 3
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        var up = { key: Qt.Key_Up, modifiers: Qt.NoModifier, accepted: false }
        verify(collectionNav.handle(down))
        compare(collection.selectedId, "B2")
        verify(collectionNav.handle(down))
        compare(collection.selectedId, "C3")
        verify(collectionNav.handle(up))
        verify(collectionNav.handle(up))
        compare(collection.selectedId, "A3")
    }

    function test_lane_forward_retains_intended_column() {
        collection.entries = [
            { id: "A0" }, { id: "A1" }, { id: "A2" }, { id: "A3" },
            { id: "B0" }, { id: "B1" }, { id: "B2" },
            { id: "C0" }, { id: "C1" }, { id: "C2" }, { id: "C3" }
        ]
        collection.rowLengths = [4, 3, 4]
        collection.modelRevision = 11
        collection.currentIndex = 3
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        verify(collectionNav.handle(down))
        compare(collection.selectedId, "B2")
        verify(collectionNav.handle(down))
        compare(collection.selectedId, "C3")
    }

    function test_revision_keeps_surviving_identity() {
        collection.entries = [
            { id: "A0" }, { id: "A1" }, { id: "A2" }, { id: "A3" },
            { id: "B0" }, { id: "B1" }, { id: "B2" }
        ]
        collection.rowLengths = [4, 3]
        collection.modelRevision = 21
        collection.currentIndex = 3
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        var up = { key: Qt.Key_Up, modifiers: Qt.NoModifier, accepted: false }
        verify(collectionNav.handle(down))
        collection.entries = [
            { id: "A0" }, { id: "A3" }, { id: "A1" }, { id: "A2" },
            { id: "B0" }, { id: "B1" }, { id: "B2" }
        ]
        collection.modelRevision = 22
        verify(collectionNav.handle(up))
        compare(collection.selectedId, "A3")
    }

    function test_region_modifier_policy_reaches_navigator_with_event_context() {
        var region = createReturnRegion()
        region.sourceAction.forceActiveFocus(Qt.OtherFocusReason)
        verify(!region.handleKey(Qt.Key_Right, Qt.ControlModifier))
        verify(region.sourceAction.activeFocus)
        verify(!region.overlayAction.activeFocus)
    }

    function test_scroll_controller_registry_tracks_rebind_and_destruction() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item {'
            + 'Flickable { id: first; width: 80; height: 80; contentWidth: 80; contentHeight: 160 }'
            + 'Flickable { id: second; width: 80; height: 80; contentWidth: 80; contentHeight: 160; y: 90 }'
            + 'C.KeyboardScrollController { id: controller; flick: first }'
            + 'property alias firstFlick: first; property alias secondFlick: second; property alias controllerItem: controller }',
            testWindow.contentItem)
        compare(Viewport.controllerFor(fixture.firstFlick), fixture.controllerItem)
        fixture.controllerItem.flick = fixture.secondFlick
        compare(Viewport.controllerFor(fixture.firstFlick), null)
        compare(Viewport.controllerFor(fixture.secondFlick), fixture.controllerItem)
        var second = fixture.secondFlick
        fixture.destroy()
        wait(10)
        compare(Viewport.controllerFor(second), null)
    }

    function test_independent_grid_rails_keep_lane_state_and_reset_at_home() {
        collection.entries = [
            { id: "A0" }, { id: "A1" }, { id: "A2" }, { id: "A3" },
            { id: "B0" }, { id: "B1" }, { id: "B2" },
            { id: "C0" }, { id: "C1" }, { id: "C2" }, { id: "C3" }
        ]
        collection.rowLengths = [4, 3, 4]
        collection.modelRevision = 31
        collection.currentIndex = 3
        var view2 = Qt.createQmlObject(
            'import QtQuick 2.15; Item { property int currentIndex: 3; property int count: 11;'
            + 'property var rowLengths: [4,3,4]; function positionViewAtIndex(i,m) {} }', testWindow.contentItem)
        var controller2 = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; C.KeyboardCollectionController {'
            + 'view: parent; orientation: "grid"; columns: 4; rowLengths: parent.rowLengths }', view2)
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        var home = { key: Qt.Key_Home, modifiers: Qt.NoModifier, accepted: false }
        verify(collectionNav.handle(down))
        verify(controller2.handle(down))
        compare(collection.currentIndex, 6)
        compare(view2.currentIndex, 6)
        verify(collectionNav.handle(down))
        verify(controller2.handle(down))
        compare(collection.currentIndex, 10)
        compare(view2.currentIndex, 10)
        verify(collectionNav.handle(home))
        collection.currentIndex = 3
        verify(collectionNav.handle(down))
        compare(collection.currentIndex, 6)
        controller2.destroy()
        view2.destroy()
    }

    function test_real_separate_rails_return_identity_after_mutation() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 440; height: 280;'
            + 'QtObject { id: sections; property var sourceOwner: null; property string sourceIdentity: ""; property int sourceIndex: -1;'
            + 'function remember(owner,index,identity) { sourceOwner=owner; sourceIndex=index; sourceIdentity=identity }'
            + 'function returnRecord(owner) { return sourceOwner && sourceOwner !== owner ? ({ owner: sourceOwner, index: sourceIndex, identity: sourceIdentity }) : null }'
            + 'function clear() { sourceOwner=null; sourceIdentity=""; sourceIndex=-1 } }'
            + 'Flickable { id: railA; objectName: "railA"; y: 20; width: 220; height: 70; contentWidth: 440; contentHeight: 70; clip: true; property var keyboardItems: aItems; property var keyboardSectionCoordinator: sections; property bool keyboardReturnOwner: true; property var aItems: [ {id:"A0"},{id:"A1"},{id:"A2"},{id:"A3"} ]; property int currentIndex: 3; function keyboardIdentityForIndex(i) { return i >= 0 && i < aItems.length ? aItems[i].id : "" } function keyboardIndexForIdentity(id) { for (var i=0;i<aItems.length;++i) if (aItems[i].id === id) return i; return -1 } function keyboardRevealIndex(i) { currentIndex=i; contentX=Math.max(0, Math.min(contentWidth-width, i*100)); return true } function keyboardItemAtIndex(i) { return repA.itemAt(i) } function sourceItem() { return repA.itemAt(currentIndex) } Row { Repeater { id: repA; model: railA.aItems; delegate: C.KeyboardAction { required property var modelData; required property int index; objectName: modelData.id; x: index * 100; width: 100; height: 60; pointerEnabled: false } } } }'
            + 'Flickable { id: railB; objectName: "railB"; y: 150; width: 300; height: 70; contentWidth: 300; contentHeight: 70; clip: true; property var keyboardItems: bItems; property var keyboardSectionCoordinator: sections; property bool keyboardReturnOwner: true; property var bItems: [ {id:"B0"},{id:"B1"},{id:"B2"} ]; property int currentIndex: 0; function keyboardIdentityForIndex(i) { return i >= 0 && i < bItems.length ? bItems[i].id : "" } function keyboardIndexForIdentity(id) { for (var i=0;i<bItems.length;++i) if (bItems[i].id === id) return i; return -1 } function keyboardRevealIndex(i) { currentIndex=i; contentX=Math.max(0, Math.min(contentWidth-width, i*100)); return true } function keyboardItemAtIndex(i) { return repB.itemAt(i) } function sourceItem() { return repB.itemAt(currentIndex) } Row { Repeater { id: repB; model: railB.bItems; delegate: C.KeyboardAction { required property var modelData; required property int index; objectName: modelData.id; x: index * 100; width: 100; height: 60; pointerEnabled: false } } } }'
            + 'C.KeyboardCollectionController { id: keysA; view: railA; orientation: "horizontal"; count: railA.aItems.length; identityForIndex: railA.keyboardIdentityForIndex; indexForIdentity: railA.keyboardIndexForIdentity; positionIndexFn: railA.keyboardRevealIndex }'
            + 'C.KeyboardCollectionController { id: keysB; view: railB; orientation: "horizontal"; count: railB.bItems.length; identityForIndex: railB.keyboardIdentityForIndex; indexForIdentity: railB.keyboardIndexForIdentity; positionIndexFn: railB.keyboardRevealIndex }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } Keys.onPressed: function(event) { nav.handle(event) }'
            + 'property alias navigator: nav; property alias a: railA; property alias b: railB }', testWindow.contentItem)
        fixture.a.focus = true
        fixture.a.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.a.activeFocus)
        fixture.a.currentIndex = 3
        verify(fixture.navigator.moveFrom(fixture.a.sourceItem(), Qt.Key_Down))
        compare(fixture.b.currentIndex, 2)
        fixture.a.aItems = [ {id: "A3"}, {id: "A0"}, {id: "A1"}, {id: "A2"} ]
        fixture.a.contentX = 100
        fixture.b.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.navigator.moveFrom(fixture.b.sourceItem(), Qt.Key_Up))
        compare(fixture.a.currentIndex, 0)
        compare(fixture.a.keyboardIdentityForIndex(fixture.a.currentIndex), "A3")
        fixture.a.aItems = [ {id: "A0"}, {id: "A1"}, {id: "A3"}, {id: "A2"} ]
        fixture.a.currentIndex = 2
        fixture.a.focus = true
        fixture.a.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.a.activeFocus)
        verify(fixture.navigator.moveFrom(fixture.a.sourceItem(), Qt.Key_Down))
        fixture.b.forceActiveFocus(Qt.OtherFocusReason)
        fixture.a.aItems = [ {id: "A0"}, {id: "A1"}, {id: "A2"} ]
        verify(fixture.navigator.moveFrom(fixture.b.sourceItem(), Qt.Key_Up))
        compare(fixture.a.currentIndex, 2)
        compare(fixture.a.keyboardIdentityForIndex(fixture.a.currentIndex), "A2")
        fixture.destroy()
    }

    function test_worldpage_rail_crossing_records_return_and_lateral_resets_it() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { id: world; width: 640; height: 360; focus: true;'
            + 'QtObject { id: sections; property var sourceOwner: null; property string sourceIdentity: ""; property int sourceIndex: -1; property real sourceOffset: 0;'
            + 'function remember(owner,index,identity,offset) { sourceOwner=owner; sourceIndex=index; sourceIdentity=identity; sourceOffset=offset }'
            + 'function returnRecord(owner) { return sourceOwner && sourceOwner !== owner ? ({ owner: sourceOwner, index: sourceIndex, identity: sourceIdentity, offset: sourceOffset }) : null }'
            + 'function clear() { sourceOwner=null; sourceIdentity=""; sourceIndex=-1; sourceOffset=0 } }'
            + 'Flickable { id: board; objectName: "worldPageScroll"; width: 640; height: 300; contentWidth: 640; contentHeight: 500; clip: true;'
            + 'Item { id: boardContent; width: 640; height: 500;'
            + 'Flickable { id: railA; objectName: "continueRail"; x: 20; y: 20; width: 320; height: 50; contentWidth: 560; contentHeight: 50; clip: true; flickableDirection: Flickable.HorizontalFlick; focusPolicy: Qt.TabFocus; property bool keyboardReturnOwner: true; property var keyboardSectionCoordinator: sections; property var keyboardItems: aItems; property var aItems: [ {id:"A0"},{id:"A1"},{id:"A2"},{id:"A3"},{id:"A4"} ]; property int currentIndex: 1; property int keyCalls: 0; property var keyboardIdentityForIndex: function(i) { return i >= 0 && i < aItems.length ? aItems[i].id : "" }; property var keyboardIndexForIdentity: function(id) { for (var i=0;i<aItems.length;++i) if (aItems[i].id === id) return i; return -1 }; property var keyboardRevealIndex: function(i) { currentIndex=i; contentX=Math.max(0, Math.min(contentWidth-width, i*110)); return true }; property var keyboardItemAtIndex: function(i) { return repA.itemAt(i) }; function sourceItem() { return repA.itemAt(currentIndex) } Keys.onPressed: function(e) { keyCalls++; if (keysA.handle(e)) e.accepted=true } Row { Repeater { id: repA; model: railA.aItems; delegate: C.KeyboardAction { required property var modelData; required property int index; objectName: modelData.id; x: index * 110; width: 100; height: 45; focusEnabled: false; pointerEnabled: false } } } }'
            + 'Flickable { id: railB; objectName: "featuredRail"; x: 20; y: 85; width: 320; height: 50; contentWidth: 340; contentHeight: 50; clip: true; flickableDirection: Flickable.HorizontalFlick; focusPolicy: Qt.TabFocus; property bool keyboardReturnOwner: true; property var keyboardSectionCoordinator: sections; property var keyboardItems: bItems; property var bItems: [ {id:"B0"},{id:"B1"},{id:"B2"} ]; property int currentIndex: 0; property int keyCalls: 0; property var keyboardIdentityForIndex: function(i) { return i >= 0 && i < bItems.length ? bItems[i].id : "" }; property var keyboardIndexForIdentity: function(id) { for (var i=0;i<bItems.length;++i) if (bItems[i].id === id) return i; return -1 }; property var keyboardRevealIndex: function(i) { currentIndex=i; contentX=Math.max(0, Math.min(contentWidth-width, i*110)); return true }; property var keyboardItemAtIndex: function(i) { return repB.itemAt(i) }; function sourceItem() { return repB.itemAt(currentIndex) } Keys.onPressed: function(e) { keyCalls++; if (keysB.handle(e)) e.accepted=true } Row { Repeater { id: repB; model: railB.bItems; delegate: C.KeyboardAction { required property var modelData; required property int index; objectName: modelData.id; x: index * 110; width: 100; height: 45; focusEnabled: false; pointerEnabled: false } } } }'
            + 'C.KeyboardCollectionController { id: keysA; view: railA; orientation: "horizontal"; count: railA.aItems.length; identityForIndex: railA.keyboardIdentityForIndex; indexForIdentity: railA.keyboardIndexForIdentity; positionIndexFn: railA.keyboardRevealIndex; keyboardSectionCoordinator: sections }'
            + 'C.KeyboardCollectionController { id: keysB; view: railB; orientation: "horizontal"; count: railB.bItems.length; identityForIndex: railB.keyboardIdentityForIndex; indexForIdentity: railB.keyboardIndexForIdentity; positionIndexFn: railB.keyboardRevealIndex; keyboardSectionCoordinator: sections }'
            + '} } property int worldKeyCalls: 0; C.KeyboardSpatialNavigator { id: nav; root: world } Keys.onPressed: function(e) { worldKeyCalls++; if (nav.handle(e)) e.accepted=true }'
            + 'property alias navigator: nav; property alias a: railA; property alias b: railB; property alias boardView: board; property alias coordinator: sections }', testWindow.contentItem)
        fixture.a.forceActiveFocus(Qt.OtherFocusReason)
        fixture.a.currentIndex = 1
        verify(fixture.a.activeFocus)
        keyClick(Qt.Key_Down)
        compare(fixture.b.currentIndex, 1)
        compare(fixture.b.keyboardIdentityForIndex(fixture.b.currentIndex), "B1")
        verify(fixture.b.activeFocus || fixture.b.children.length > 0)
        keyClick(Qt.Key_Up)
        compare(fixture.a.currentIndex, 1)
        compare(fixture.a.keyboardIdentityForIndex(fixture.a.currentIndex), "A1")
        fixture.coordinator.remember(fixture.a, 1, "A1", fixture.a.contentX)
        var beforeHiddenIndex = fixture.a.currentIndex
        var beforeHiddenOffset = fixture.a.contentX
        fixture.a.visible = false
        verify(!fixture.navigator._restoreSectionReturn(fixture.b.sourceItem(), Qt.Key_Up, Qt.BacktabFocusReason))
        compare(fixture.a.currentIndex, beforeHiddenIndex)
        compare(fixture.a.contentX, beforeHiddenOffset)
        fixture.a.visible = true

        fixture.a.forceActiveFocus(Qt.OtherFocusReason)
        fixture.a.currentIndex = 0
        verify(fixture.a.activeFocus)
        keyClick(Qt.Key_Down)
        compare(fixture.b.currentIndex, 0)
        keyClick(Qt.Key_Right)
        compare(fixture.b.currentIndex, 1)
        keyClick(Qt.Key_Up)
        compare(fixture.a.currentIndex, 2)
        compare(fixture.a.keyboardIdentityForIndex(fixture.a.currentIndex), "A2")
        fixture.destroy()
    }

    function test_reorder_returns_same_id_and_removal_uses_nearest_peer() {
        keyClick(Qt.Key_Down)
        collection.entries = [
            { id: "A0", section: "A" },
            { id: "A3", section: "A" },
            { id: "A1", section: "A" },
            { id: "A2", section: "A" },
            { id: "B0", section: "B" },
            { id: "B1", section: "B" },
            { id: "B2", section: "B" }
        ]
        keyClick(Qt.Key_Up)
        compare(collection.selectedId, "A3")
        compare(collection.currentIndex, 1)

        collection.entries = [
            { id: "A0", section: "A" },
            { id: "A1", section: "A" },
            { id: "A2", section: "A" },
            { id: "B0", section: "B" },
            { id: "B1", section: "B" },
            { id: "B2", section: "B" }
        ]
        collection.rowLengths = [3, 3]
        collection.modelRevision = 2
        collection.currentIndex = 5
        keyClick(Qt.Key_Up)
        compare(collection.selectedId, "A2")
        compare(collection.currentIndex, 2)
    }

    function test_true_boundaries_remain_unaccepted() {
        collection.currentIndex = 6
        var down = { key: Qt.Key_Down, modifiers: Qt.NoModifier, accepted: false }
        verify(!collectionNav.handle(down))
        verify(!down.accepted)
        compare(collection.currentIndex, 6)

        collection.currentIndex = 0
        var up = { key: Qt.Key_Up, modifiers: Qt.NoModifier, accepted: false }
        verify(!collectionNav.handle(up))
        verify(!up.accepted)
        compare(collection.currentIndex, 0)
    }

    function test_virtualized_grid_keeps_owner_focus_until_delegate_realizes() {
        nativeGrid.forceActiveFocus(Qt.OtherFocusReason)
        nativeGrid.currentIndex = 0
        keyClick(Qt.Key_Down)
        compare(nativeGrid.currentIndex, 4)
        verify(nativeGrid.activeFocus)
        verify(nativeGrid.currentItem !== null)
    }

    function test_repeat_release_cancels_pending_landing_without_overshoot() {
        var region = createReturnRegion()
        var navigator = region.spatialNavigator
        region.sourceAction.forceActiveFocus(Qt.OtherFocusReason)
        var generation = navigator.beginNavigation(Qt.Key_Down)
        compare(navigator.beginNavigation(Qt.Key_Down), generation)
        verify(navigator.deferLanding(region.overlayAction, Qt.Key_Down,
                                      Qt.TabFocusReason, generation))
        navigator.handleRelease({ key: Qt.Key_Down })
        verify(!navigator.settlePendingLanding())
        verify(!region.overlayAction.activeFocus)
        verify(navigator.navigationGeneration > generation)
    }

    function test_opposite_direction_replaces_pending_input_generation() {
        var region = createReturnRegion()
        var navigator = region.spatialNavigator
        region.sourceAction.forceActiveFocus(Qt.OtherFocusReason)
        var downGeneration = navigator.beginNavigation(Qt.Key_Down)
        verify(navigator.deferLanding(region.overlayAction, Qt.Key_Down,
                                      Qt.TabFocusReason, downGeneration))
        var upGeneration = navigator.beginNavigation(Qt.Key_Up)
        verify(upGeneration > downGeneration)
        verify(!navigator.isNavigationGenerationCurrent(downGeneration, Qt.Key_Down))
        verify(navigator.isNavigationGenerationCurrent(upGeneration, Qt.Key_Up))
        verify(!navigator.settlePendingLanding())
    }

    function test_escape_round_trip_restores_stable_focus_and_scroll_offset() {
        var region = createReturnRegion()
        region.sourceAction.forceActiveFocus(Qt.OtherFocusReason)
        region.scroll.contentY = 140
        verify(region.rememberFocus(region.sourceAction))
        region.overlayAction.forceActiveFocus(Qt.OtherFocusReason)
        verify(region.restoreFocus())
        verify(region.sourceAction.activeFocus)
        compare(region.scroll.contentY, 140)
    }

    function test_escape_is_one_level_and_removed_invoker_falls_back() {
        var region = createReturnRegion()
        var escapeSpy = Qt.createQmlObject(
            'import QtTest 1.3; SignalSpy { signalName: "escapeRequested" }',
            testCase)
        escapeSpy.target = region
        region.sourceAction.forceActiveFocus(Qt.OtherFocusReason)
        verify(region.handleKey(Qt.Key_Escape, Qt.NoModifier))
        compare(escapeSpy.count, 1)
        verify(region.rememberFocus(region.sourceAction))
        region.overlayAction.visible = false
        region.sourceAction.destroy()
        wait(10)
        verify(region.restoreFocus())
        verify(region.fallbackAction.activeFocus)
        escapeSpy.destroy()
    }

    function test_focus_and_route_loss_invalidate_pending_landing() {
        var region = createReturnRegion()
        var navigator = region.spatialNavigator
        var generation = navigator.beginNavigation(Qt.Key_Down)
        verify(navigator.deferLanding(region.overlayAction, Qt.Key_Down,
                                      Qt.TabFocusReason, generation))
        region.visible = false
        wait(5)
        verify(!navigator.settlePendingLanding())
        region.visible = true
        region.forceActiveFocus(Qt.OtherFocusReason)
        verify(navigator.navigationGeneration > generation)
    }
}

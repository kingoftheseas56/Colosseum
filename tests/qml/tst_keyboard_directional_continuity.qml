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

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "KeyboardSpatialNavigator"
    when: windowShown

    Window {
        id: testWindow
        width: 640
        height: 480
        visible: true

        Item {
            id: focusRoot
            anchors.fill: parent

            Colosseum.KeyboardAction {
                id: source
                objectName: "source"
                x: 100; y: 60; width: 90; height: 44
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: closeDiagonal
                objectName: "closeDiagonal"
                x: 205; y: 115; width: 90; height: 44
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: alignedDown
                objectName: "alignedDown"
                x: 100; y: 205; width: 90; height: 44
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: hiddenNamedTwin
                objectName: "namedTwin"
                x: 15; y: 15; width: 90; height: 44
                visible: false
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: visibleNamedTwin
                objectName: "namedTwin"
                x: 15; y: 205; width: 90; height: 44
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: alignedRight
                objectName: "alignedRight"
                x: 315; y: 60; width: 90; height: 44
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: hiddenDown
                objectName: "hiddenDown"
                x: 100; y: 130; width: 90; height: 44
                visible: false
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: disabledRight
                objectName: "disabledRight"
                x: 225; y: 60; width: 70; height: 44
                enabled: false
                pointerEnabled: false
            }

            Colosseum.KeyboardSpatialNavigator {
                id: spatial
                root: focusRoot
            }
        }
    }

    SignalSpy { id: boundarySpy; target: spatial; signalName: "boundaryRequested" }

    function init() {
        testWindow.requestActivate()
        boundarySpy.clear()
        source.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        verify(source.activeFocus)
    }

    function test_direction_alignment_beats_raw_distance() {
        verify(spatial.move(Qt.Key_Down))
        verify(alignedDown.activeFocus)
    }

    function test_hidden_and_disabled_targets_are_skipped() {
        verify(spatial.move(Qt.Key_Right))
        verify(alignedRight.activeFocus)
    }

    function test_boundary_is_not_swallowed() {
        alignedRight.forceActiveFocus(Qt.OtherFocusReason)
        wait(5)
        verify(!spatial.move(Qt.Key_Right))
        compare(boundarySpy.count, 1)
        compare(boundarySpy.signalArguments[0][0], Qt.Key_Right)
    }

    function test_up_returns_to_geometrically_aligned_source() {
        alignedDown.forceActiveFocus(Qt.OtherFocusReason)
        wait(5)
        verify(spatial.move(Qt.Key_Up))
        verify(source.activeFocus)
    }

    function test_named_focus_origin_is_deterministic() {
        verify(spatial.focusNamed("alignedDown", Qt.TabFocusReason))
        verify(alignedDown.activeFocus)
        verify(spatial.focusNamed("namedTwin", Qt.TabFocusReason))
        verify(visibleNamedTwin.activeFocus)
        verify(!spatial.focusNamed("doesNotExist", Qt.TabFocusReason))
    }

    function test_partially_clipped_center_inside_target_is_not_landed() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 240; height: 220;'
            + 'property alias navigator: nav; property alias source: source; property alias target: target;'
            + 'Item { id: clipA; x: 20; y: 20; width: 180; height: 100; clip: true;'
            + 'Item { id: clipB; x: 90; y: 0; width: 180; height: 100; clip: true;'
            + 'C.KeyboardAction { id: target; objectName: "mostlyClippedTarget"; x: 20; y: 20; width: 140; height: 60; pointerEnabled: false } } }'
            + 'C.KeyboardAction { id: source; objectName: "isolatedSource"; x: 20; y: 0; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } }', focusRoot)
        fixture.source.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.source.activeFocus)
        verify(!fixture.navigator._centerVisibleThroughClips(fixture.target))
        verify(!fixture.navigator._land(fixture.target, Qt.Key_Down,
                                       Qt.OtherFocusReason, 0))
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }

    function test_candidate_ranking_skips_scroll_opt_out_target() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 300; height: 460;'
            + 'property alias navigator: nav; property alias source: source; property alias blockedTarget: blockedTarget; property alias legal: legal;'
            + 'Flickable { id: blocked; x: 100; y: 150; width: 160; height: 100; contentWidth: width; contentHeight: height; clip: true;'
            + 'C.KeyboardScrollController { flick: blocked; arrowScrolling: false }'
            + 'C.KeyboardAction { id: blockedTarget; objectName: "blockedTarget"; x: 0; y: 10; width: 100; height: 40; pointerEnabled: false } }'
            + 'C.KeyboardAction { id: source; objectName: "isolatedSource"; x: 100; y: 40; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: legal; objectName: "legalTarget"; x: 120; y: 320; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } }', focusRoot)
        var candidate = fixture.navigator.targetFrom(fixture.source, Qt.Key_Down)
        verify(candidate === fixture.legal)
        fixture.destroy()
    }

    function test_candidate_ranking_skips_rotated_target() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 300; height: 460;'
            + 'property alias navigator: nav; property alias source: source; property alias rotatedTarget: rotatedTarget; property alias legal: legal;'
            + 'Item { id: rotated; x: 90; y: 150; width: 160; height: 100; rotation: 10;'
            + 'C.KeyboardAction { id: rotatedTarget; objectName: "rotatedTarget"; x: 10; y: 10; width: 100; height: 40; pointerEnabled: false } }'
            + 'C.KeyboardAction { id: source; objectName: "isolatedSource"; x: 100; y: 40; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: legal; objectName: "legalTarget"; x: 120; y: 320; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } }', focusRoot)
        var candidate = fixture.navigator.targetFrom(fixture.source, Qt.Key_Down)
        verify(candidate === fixture.legal)
        fixture.destroy()
    }
}

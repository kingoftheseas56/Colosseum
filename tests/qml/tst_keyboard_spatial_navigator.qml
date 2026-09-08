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
}

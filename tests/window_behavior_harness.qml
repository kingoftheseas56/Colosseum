// Behavioral test for WindowBehavior: drives the actual drag / double-click / edge-resize
// paths against a fake controller and asserts the controller was called. Run with:
//   qmltestrunner -input tests/window_behavior_harness.qml
// (qmltestrunner loads into a QQuickView, so the root must be an Item; the shell under
//  test is a child Window.)
import QtQuick
import QtQuick.Window
import QtTest
import "../qml"

Item {
    id: root
    width: 200
    height: 200

    Window {
        id: shell
        width: 1280
        height: 720
        visibility: Window.Windowed
        visible: true

        QtObject {
            id: fakeController
            property bool shellWindowed: true
            property bool pipMode: false
            property int moveCalls: 0
            property int maximizeCalls: 0
            property int resizeCalls: 0
            function startSystemMove(window) { moveCalls++; return true }
            function toggleMaximized(window) { maximizeCalls++ }
            function startSystemResize(window, edges) { resizeCalls++; return true }
        }

        Item { id: fakeTopBar; width: parent.width - 80; height: 56; anchors.horizontalCenter: parent.horizontalCenter; y: 30 }

        WindowBehavior {
            id: behavior
            shell: shell
            dragSurface: fakeTopBar
            controller: fakeController
        }
    }

    TestCase {
        name: "WindowBehavior"
        when: windowShown

        function initTestCase() {
            tryVerify(function() { return shell.visible && shell.active }, 3000)
        }

        function test_0_interactive_state() {
            // Preconditions: with a windowed, non-PiP controller the move region and the
            // resize zones must be live, or the behavioral checks below prove nothing.
            verify(behavior.shellInteractive, "move region must be enabled while windowed")
            verify(behavior.resizable, "resize zones must be enabled while windowed")
        }

        function test_1_topbar_doubleclick_maximizes() {
            var before = fakeController.maximizeCalls
            mouseDoubleClickSequence(fakeTopBar, fakeTopBar.width / 2, fakeTopBar.height / 2, Qt.LeftButton)
            verify(fakeController.maximizeCalls > before, "TopBar double-click must call toggleMaximized")
        }

        function test_2_edge_press_calls_resize() {
            var before = fakeController.resizeCalls
            // The left edge zone is a 6px strip; press inside it, mid-height (clear of corners).
            mousePress(shell.contentItem, 2, shell.height / 2, Qt.LeftButton)
            mouseRelease(shell.contentItem, 2, shell.height / 2, Qt.LeftButton)
            verify(fakeController.resizeCalls > before, "edge press must call startSystemResize")
        }

        function test_3_topbar_drag_calls_move() {
            var before = fakeController.moveCalls
            // Drag unused TopBar space (below the 6px top edge zone) -> native system move.
            mouseDrag(fakeTopBar, fakeTopBar.width / 2, fakeTopBar.height / 2, 70, 30, Qt.LeftButton)
            verify(fakeController.moveCalls > before, "unused TopBar drag must call startSystemMove")
        }
    }
}

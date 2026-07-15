import QtQuick
import QtQuick.Window
import "../qml"

Window {
    id: shell
    width: 1280
    height: 720
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
        shell: shell
        dragSurface: fakeTopBar
        controller: fakeController
    }

    Timer {
        interval: 100
        running: true
        onTriggered: {
            console.log("[window-behavior-harness] PASS")
            Qt.quit()
        }
    }
}

import QtQuick
import QtTest
import "../../qml"

Item {
    width: 200; height: 200
    KeyboardAction {
        id: action
        anchors.fill: parent
        contextEnabled: true
    }
    SignalSpy { id: primarySpy; target: action; signalName: "triggered" }
    SignalSpy { id: contextSpy; target: action; signalName: "contextRequested" }
    TestCase {
        name: "KeyboardActionTouch"
        when: windowShown
        function init() { primarySpy.clear(); contextSpy.clear() }
        function test_touch_invokes_only_primary() {
            touchEvent(action).press(0, action, 100, 100).commit()
            touchEvent(action).release(0, action, 100, 100).commit()
            wait(50)
            compare(primarySpy.count, 1)
            compare(contextSpy.count, 0)
        }
        function test_right_mouse_invokes_only_context() {
            mouseClick(action, 100, 100, Qt.RightButton)
            compare(contextSpy.count, 1)
            compare(primarySpy.count, 0)
        }
    }
}

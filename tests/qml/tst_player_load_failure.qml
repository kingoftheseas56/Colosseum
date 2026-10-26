import QtQuick
import QtTest
import "../../qml" as Product

TestCase {
    name: "PlayerLoadFailure"
    when: windowShown
    visible: true
    width: 800
    height: 480
    property var face: null
    Component { id: factory; Product.PlayerLoadFailure { width: 800; height: 480; visible: false } }
    SignalSpy { id: retrySpy; target: face; signalName: "retryRequested" }
    SignalSpy { id: backSpy; target: face; signalName: "backRequested" }
    function init() {
        face = createTemporaryObject(factory, this)
        verify(face !== null)
        retrySpy.clear()
        backSpy.clear()
        face.visible = true
        waitForRendering(face)
    }
    function cleanup() { face = null }
    function test_retry_pointer() {
        mouseClick(findChild(face, "playerLoadRetry"))
        compare(retrySpy.count, 1)
        compare(backSpy.count, 0)
    }
    function test_back_pointer() {
        mouseClick(findChild(face, "playerLoadBack"))
        compare(backSpy.count, 1)
        compare(retrySpy.count, 0)
    }
    function test_escape_returns_to_shell() {
        keyClick(Qt.Key_Escape)
        compare(backSpy.count, 1)
    }
    function test_enter_retries() {
        keyClick(Qt.Key_Return)
        compare(retrySpy.count, 1)
    }
}

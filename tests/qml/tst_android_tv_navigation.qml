import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidTvNavigation"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
        property bool televisionMode: true
    }

    Component {
        id: actionComponent
        App.KeyboardAction {
            width: 100
            height: 48
            accessibleName: "TV action"
            onTriggered: testCase.triggerCount++
        }
    }

    property var action: null
    property int triggerCount: 0
    function init() {
        triggerCount = 0
        action = actionComponent.createObject(testWindow.contentItem)
        verify(action !== null)
        action.forceActiveFocus(Qt.OtherFocusReason)
        wait(0)
    }

    function cleanup() {
        if (action) action.destroy()
        action = null
    }

    function test_select_activates_semantic_action() {
        compare(action.televisionMode, true)
        compare(action.focusFrameWidth, 4)
        keyClick(Qt.Key_Select)
        compare(triggerCount, 1)
    }
}

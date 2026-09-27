import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    name: "K06AccountSystemEvents"
    when: windowShown
    Window { id: testWindow; width: 1280; height: 720; visible: true }

    QtObject {
        id: fakeController
        property string mode: "localOnly"
        property string username: ""
        property int pendingOutboxCount: 0
        property string syncState: "inactive"
        function logoutCurrent() {}
        function returnToSignIn() {}
    }

    Component {
        id: flyoutComp
        Account.AccountFlyout {
            width: 1280; height: 720
            controller: fakeController
        }
    }

    property var subject: null
    property var focusHost: null

    Component {
        id: focusHostComp
        Item {
            width: 1280
            height: 720
            property int downCount: 0
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down)
                    downCount += 1
            }
        }
    }

    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found) return found
        }
        return null
    }

    function init() { testWindow.requestActivate(); wait(20) }
    function cleanup() {
        if (subject) subject.destroy()
        subject = null
        if (focusHost) focusHost.destroy()
        focusHost = null
    }

    function test_local_flyout_escape_and_focus_restore_path() {
        subject = flyoutComp.createObject(testWindow.contentItem)
        verify(subject !== null)
        subject.open()
        tryCompare(subject, "visible", true)
        var sessionAction = byName(subject, "accountFlyoutSessionAction")
        verify(sessionAction !== null)
        tryCompare(sessionAction, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        tryCompare(subject, "visible", false)
    }

    function test_local_flyout_contains_unhandled_down_at_overlay_boundary() {
        focusHost = focusHostComp.createObject(testWindow.contentItem)
        verify(focusHost !== null)
        subject = flyoutComp.createObject(focusHost)
        verify(subject !== null)
        subject.open()
        tryCompare(subject, "visible", true)
        var sessionAction = byName(subject, "accountFlyoutSessionAction")
        verify(sessionAction !== null)
        tryCompare(sessionAction, "activeFocus", true)

        keyClick(Qt.Key_Down)

        compare(focusHost.downCount, 0,
                "bare Down must be contained by AccountFlyout, not delivered to its parent")
        tryCompare(sessionAction, "activeFocus", true)
    }
    Component { id: signalSpy; SignalSpy {} }
}

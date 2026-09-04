import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account
import "../../qml" as Colosseum

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

    Component {
        id: updateComp
        Colosseum.UpdatePage {
            width: 1280; height: 720
            updates: null
        }
    }
    property var subject: null

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
    function test_update_chrome_keyboard_activation() {
        subject = updateComp.createObject(testWindow.contentItem)
        verify(subject !== null)
        wait(20)
        var minimize = byName(subject, "colosseumUpdateMinimize")
        var fullscreen = byName(subject, "colosseumUpdateFullscreen")
        var close = byName(subject, "colosseumUpdateClose")
        verify(minimize !== null && fullscreen !== null && close !== null)

        var minimizeSpy = signalSpy.createObject(testWindow, { target: subject, signalName: "minimizeRequested" })
        var fullscreenSpy = signalSpy.createObject(testWindow, { target: subject, signalName: "fullscreenRequested" })
        var closeSpy = signalSpy.createObject(testWindow, { target: subject, signalName: "closeRequested" })

        minimize.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        compare(minimizeSpy.count, 1)
        fullscreen.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        compare(fullscreenSpy.count, 1)
        close.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Enter)
        compare(closeSpy.count, 1)

        minimizeSpy.destroy(); fullscreenSpy.destroy(); closeSpy.destroy()
    }

    function test_update_escape_requests_back() {
        subject = updateComp.createObject(testWindow.contentItem)
        verify(subject !== null)
        wait(20)
        var backSpy = signalSpy.createObject(testWindow, { target: subject, signalName: "backRequested" })
        var primary = byName(subject, "colosseumUpdatePrimaryAction")
        verify(primary !== null)
        subject.takeKeyboardFocus()
        tryCompare(primary, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        compare(backSpy.count, 1)
        backSpy.destroy()
    }

    Component { id: signalSpy; SignalSpy {} }
}

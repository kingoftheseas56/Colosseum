import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "PhoneAccountKeyboard"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        property bool androidHost: true
        visible: true
    }

    QtObject {
        id: fakeInputMethod
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
    }

    Component {
        id: frameComponent
        Account.AccountPageFrame {
            eyebrow: "ACCOUNT ACCESS"
            headline: "Sign in to your account"
            detail: Array(46).join("Your account settings and connected services remain available while you sign in. ")

            TextField {
                objectName: "phoneAccountUsername"
                width: parent.width
                height: 56
                placeholderText: "Username"
            }

            TextField {
                objectName: "phoneAccountPassword"
                width: parent.width
                height: 56
                placeholderText: "Password"
            }
        }
    }

    property var frame: null

    function byName(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function isVisibleInScrollViewport(item, scroller) {
        var position = item.mapToItem(scroller, 0, 0)
        return position.y >= 0 && position.y + item.height <= scroller.height
    }

    function init() {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.requestActivate()
        fakeInputMethod.visible = false
        fakeInputMethod.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        frame = frameComponent.createObject(testWindow.contentItem, {
            "width": testWindow.width,
            "height": testWindow.height,
            "inputMethod": fakeInputMethod
        })
        verify(frame !== null)
        wait(30)
    }

    function cleanup() {
        if (frame)
            frame.destroy()
        frame = null
    }

    function focusRevealsUsername(width, height) {
        testWindow.width = width
        testWindow.height = height
        testWindow.androidHost = true
        frame.width = width
        frame.height = height
        wait(30)

        var scroller = byName(frame, "accountPageFrameScrollRegion")
        var username = byName(frame, "phoneAccountUsername")
        verify(scroller !== null && username !== null)
        verify(!isVisibleInScrollViewport(username, scroller), "fixture must start with username below the viewport")

        username.forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        verify(username.activeFocus, "username received ordinary focus")
        verify(isVisibleInScrollViewport(username, scroller), "focused username should be revealed by the actual frame scroller")
    }

    function test_portrait_focus_reveals_username() {
        focusRevealsUsername(354, 792)
    }
    function test_tablet_ime_open_and_focus_switch() {
        keyboardOpenKeepsCurrentAndNextFocusedFieldVisible(800,1280,480)
    }

    function test_short_landscape_focus_reveals_username() {
        focusRevealsUsername(792, 354)
    }

    function keyboardOpenKeepsCurrentAndNextFocusedFieldVisible(width, height, keyboardHeight) {
        testWindow.width = width
        testWindow.height = height
        testWindow.androidHost = true
        frame.width = width
        frame.height = height
        wait(30)

        var scroller = byName(frame, "accountPageFrameScrollRegion")
        var username = byName(frame, "phoneAccountUsername")
        var password = byName(frame, "phoneAccountPassword")
        verify(scroller !== null && username !== null && password !== null)
        verify(frame.phoneLayout, "Android host and short edge should enable phone layout")

        username.forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        verify(isVisibleInScrollViewport(username, scroller), "ordinary username focus should reveal the field before IME open")

        var previousHeight = scroller.height
        fakeInputMethod.keyboardRectangle = Qt.rect(0, height - keyboardHeight, width, keyboardHeight)
        fakeInputMethod.visible = true
        wait(60)
        verify(scroller.height < previousHeight, "phone scroller should reserve the visible keyboard overlap")
        verify(isVisibleInScrollViewport(username, scroller), "current username should remain visible when the IME opens")

        password.forceActiveFocus(Qt.OtherFocusReason)
        wait(60)
        verify(password.activeFocus, "password received focus while the IME is open")
        verify(isVisibleInScrollViewport(password, scroller), "newly focused password should remain above the keyboard")
    }

    function test_portrait_ime_open_and_focus_switch() {
        keyboardOpenKeepsCurrentAndNextFocusedFieldVisible(354, 792, 260)
    }

    function test_short_landscape_ime_open_and_focus_switch() {
        keyboardOpenKeepsCurrentAndNextFocusedFieldVisible(792, 354, 220)
    }

    function missingInsetKeepsFocusedControlsAtTop(width, height) {
        testWindow.width = width
        testWindow.height = height
        testWindow.androidHost = true
        frame.width = width
        frame.height = height
        fakeInputMethod.visible = false
        fakeInputMethod.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        wait(30)

        var scroller = byName(frame, "accountPageFrameScrollRegion")
        var username = byName(frame, "phoneAccountUsername")
        var password = byName(frame, "phoneAccountPassword")
        verify(scroller !== null && username !== null && password !== null)
        verify(frame.phoneLayout, "Android host and short edge should enable phone layout")

        fakeInputMethod.visible = true
        wait(30)
        compare(frame.keyboardOverlap, 0, "visible IME with empty rectangle reports no geometric overlap")
        username.forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        verify(username.activeFocus, "username received focus while the IME is visible without a rectangle")
        var usernameTop = username.mapToItem(scroller, 0, 0).y
        verify(usernameTop >= 0 && usernameTop <= 24,
               "focused username should park at the top when the visible IME reports no inset")

        password.forceActiveFocus(Qt.OtherFocusReason)
        wait(60)
        verify(password.activeFocus, "password received focus while the visible IME has no rectangle")
        compare(frame.keyboardOverlap, 0)
        var passwordTop = password.mapToItem(scroller, 0, 0).y
        verify(passwordTop >= 0 && passwordTop <= 24,
               "focused password should park at the top when the visible IME reports no inset")
    }

    function test_portrait_visible_ime_without_rectangle_uses_fallback() {
        missingInsetKeepsFocusedControlsAtTop(354, 792)
    }

    function test_short_landscape_visible_ime_without_rectangle_uses_fallback() {
        missingInsetKeepsFocusedControlsAtTop(792, 354)
    }

    function test_desktop_does_not_reserve_mock_keyboard_space() {
        testWindow.width = 1280
        testWindow.height = 900
        testWindow.androidHost = false
        frame.width = testWindow.width
        frame.height = testWindow.height
        wait(30)

        var scroller = byName(frame, "accountPageFrameScrollRegion")
        verify(scroller !== null)
        verify(!frame.phoneLayout, "desktop host should remain outside the phone gate")
        var desktopHeight = scroller.height

        fakeInputMethod.keyboardRectangle = Qt.rect(0, 600, testWindow.width, 300)
        fakeInputMethod.visible = true
        wait(60)
        compare(frame.keyboardOverlap, 0)
        compare(scroller.height, desktopHeight)

        var desktopContentHeight = scroller.contentHeight
        var desktopContentY = scroller.contentY
        var username = byName(frame, "phoneAccountUsername")
        var password = byName(frame, "phoneAccountPassword")
        verify(username !== null && password !== null)
        fakeInputMethod.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        username.forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        password.forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        compare(frame.keyboardOverlap, 0)
        compare(scroller.height, desktopHeight)
        compare(scroller.contentHeight, desktopContentHeight,
                "desktop focus should not add the phone no-inset scroll range")
        compare(scroller.contentY, desktopContentY,
                "desktop focus should not trigger phone no-inset scrolling")
    }
}

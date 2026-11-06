import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/account" as Account

// WT-02: actual AccountSignIn/AccountCreate through the shared AccountPageFrame with a
// deterministic IME (logical keyboard rectangle). No credentials are submitted; the
// native vault is not involved. Real keyboards/floating IMEs remain device checks.
TestCase {
    id: testCase
    name: "AndroidAccountKeyboard"
    when: windowShown

    Window {
        id: testWindow
        width: 1185; height: 533; visible: true
        property bool androidHost: true
        Item { id: surface; anchors.fill: parent }
    }
    QtObject {
        id: ime
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
    }
    Component { id: signInComponent; Account.AccountSignIn { anchors.fill: parent; inputMethod: ime; controller: fakeController } }
    Component { id: createComponent; Account.AccountCreate { anchors.fill: parent; inputMethod: ime; controller: fakeController } }

    function init() {
        ime.visible = false
        ime.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        testWindow.width = 1185; testWindow.height = 533
        testWindow.androidHost = true
        testWindow.requestActivate()
        tryVerify(function () { return testWindow.active }, 1000)
    }
    QtObject { id: fakeController; property string lastErrorMessage: ""; property string mode: "localOnly" }
    function find(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], name); if (f) return f }
        return null
    }
    function visibleIn(item, frame, bottom) {
        const scroller = find(frame, "accountPageFrameScrollRegion")
        const p = item.mapToItem(frame, 0, 0)
        return p.y >= scroller.y - 0.5 && p.y + item.height <= Math.min(bottom, scroller.y + scroller.height) + 0.5
    }
    function showKeyboard(height) {
        ime.keyboardRectangle = Qt.rect(0, testWindow.height - height, testWindow.width, height)
        ime.visible = true
    }

    function test_focused_last_field_and_submit_reachable_above_keyboard_data() {
        return [{ tag: "zoom1.20", w: 1185, h: 533, kb: 230 },
                { tag: "zoom1.15", w: 1237, h: 556, kb: 240 },
                { tag: "portrait", w: 412, h: 915, kb: 330 }]
    }
    function test_focused_last_field_and_submit_reachable_above_keyboard(data) {
        testWindow.width = data.w; testWindow.height = data.h
        const frame = createTemporaryObject(signInComponent, surface)
        verify(frame !== null)
        waitForRendering(frame)
        const password = find(frame, "accountSignInPassword")
        verify(password !== null)
        password.text = "draft-not-submitted"
        password.forceActiveFocus(Qt.OtherFocusReason)
        showKeyboard(data.kb)
        tryCompare(frame, "keyboardOverlap", data.kb)
        const kbTop = data.h - data.kb
        tryVerify(function () { return visibleIn(password, frame, kbTop) }, 1000, "focused field above keyboard: active="
                  + password.activeFocus + " focused=" + frame.focusedTextControl + " y=" + password.mapToItem(frame, 0, 0).y
                  + " scrollerH=" + find(frame, "accountPageFrameScrollRegion").height)
        // The following action is reachable by scrolling within the reduced viewport.
        const scroller = find(frame, "accountPageFrameScrollRegion")
        scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height)
        const submit = find(frame, "accountSignInSubmit")
        verify(visibleIn(submit, frame, kbTop), "Continue reachable above keyboard")
        compare(password.text, "draft-not-submitted")
        // Switch to the first field: it is revealed again.
        const username = find(frame, "accountSignInUsername")
        username.forceActiveFocus(Qt.OtherFocusReason)
        tryVerify(function () { return visibleIn(username, frame, kbTop) }, 1000, "upper field revealed")
        // Hide the keyboard: no inset remains and the draft survives.
        ime.visible = false
        tryCompare(frame, "keyboardOverlap", 0)
        compare(scroller.height, frame.height)
        compare(password.text, "draft-not-submitted")
    }

    function test_zero_rectangle_ime_still_lifts_focused_field() {
        const frame = createTemporaryObject(createComponent, surface)
        verify(frame !== null)
        waitForRendering(frame)
        const scroller = find(frame, "accountPageFrameScrollRegion")
        // Last editable field in the create form.
        let fields = []
        const stack = [frame]
        while (stack.length) {
            const it = stack.shift()
            if (it.cursorRectangle !== undefined && it.visible && it.echoMode !== undefined) fields.push(it)
            const k = it.children || []
            for (let i = 0; i < k.length; ++i) stack.push(k[i])
        }
        verify(fields.length >= 2)
        fields.sort(function (a, b) { return a.mapToItem(frame, 0, 0).y - b.mapToItem(frame, 0, 0).y })
        const last = fields[fields.length - 1]
        ime.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        ime.visible = true
        last.forceActiveFocus(Qt.OtherFocusReason)
        compare(frame.keyboardOverlap, 0)
        tryVerify(function () {
            const p = last.mapToItem(frame, 0, 0)
            return p.y >= 0 && p.y <= 40
        }, 1000, "focused field lifted near the top when the IME reports no rectangle")
    }

    function test_desktop_gets_no_inset() {
        testWindow.androidHost = false
        const frame = createTemporaryObject(signInComponent, surface, { touchInput: false })
        waitForRendering(frame)
        const scroller = find(frame, "accountPageFrameScrollRegion")
        const before = scroller.contentHeight
        showKeyboard(230)
        find(frame, "accountSignInPassword").forceActiveFocus(Qt.OtherFocusReason)
        wait(50)
        compare(frame.keyboardOverlap, 0)
        compare(scroller.height, frame.height)
        compare(scroller.contentY, 0)
        compare(scroller.contentHeight, before)
    }
}

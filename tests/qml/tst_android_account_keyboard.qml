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
    Component { id: profileComponent; Account.AccountProfilePage { anchors.fill: parent; controller: fakeController; active: true } }
    Component { id: securityComponent; Account.AccountSecurityPage { anchors.fill: parent; controller: fakeController; active: true } }
    Component { id: recoveryComponent; Account.AccountRecoveryPage { anchors.fill: parent; controller: fakeController; active: true } }

    function init() {
        ime.visible = false
        ime.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        testWindow.width = 1185; testWindow.height = 533
        testWindow.androidHost = true
        testWindow.requestActivate()
        tryVerify(function () { return testWindow.active }, 1000)
    }
    QtObject {
        id: fakeController
        property string lastErrorMessage: ""
        property string mode: "signedIn"
        property string username: "Synthetic User"
        property string avatarId: "initial"
        property bool newDeviceProtection: true
        property bool signOutSyncWarningPending: false
        signal approvalRequestsChanged(var requests)
        signal passwordChangeSucceeded()
        signal recoveryKeyReplacementSucceeded()
        signal recoveryKeyReplacementFailed(string message)
        signal usernameRenameSucceeded()
        signal usernameRenameFailed(string message)
        signal builtinAvatarChangeSucceeded()
        signal builtinAvatarChangeFailed(string message)
        signal signedOut()
        signal currentDeviceLocked()
        signal accountError(string category, string code, string message)
        function refreshProfile() {}
        function refreshApprovals() {}
        function renameUsername(value) {}
        function setBuiltinAvatar(value) {}
        function setNewDeviceProtection(value) {}
        function changePassword(currentPassword, newPassword) {}
        function replaceRecoveryKey(password) {}
        function logoutEverywhere() {}
        function decideApproval(kind, challengeId, approve) {}
    }
    QtObject { id: nativeKeyboard; property real dockedKeyboardTop: -1 }
    Component {
        id: sampledKeyboardComponent
        QtObject {
            property real dockedKeyboardTop: -1
            property real nextTop: 303
            property int sampleCalls: 0
            function sampleDockedKeyboard() { ++sampleCalls; dockedKeyboardTop = nextTop }
        }
    }
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

    function centreCases() {
        return [
            { tag: "profile", component: profileComponent, field: "accountProfileUsername", scroll: "accountProfileScrollRegion" },
            { tag: "security", component: securityComponent, field: "securityConfirmPassword", scroll: "accountSecurityScrollRegion" },
            { tag: "recovery", component: recoveryComponent, field: "recoveryCurrentPassword", scroll: "accountRecoveryScrollRegion" }
        ]
    }
    function createCentre(data) {
        const page = createTemporaryObject(data.component, surface)
        verify(page !== null)
        // Pre-fix pages lack this property: the red test must fail on geometry, not setup.
        if (page.inputMethod !== undefined) page.inputMethod = ime
        if (data.tag === "security") page.openPasswordEditor()
        if (data.tag === "recovery") page.beginReplacement()
        waitForRendering(page)
        const field = find(page, data.field)
        verify(field !== null)
        field.text = "synthetic-draft"
        field.forceActiveFocus(Qt.OtherFocusReason)
        return { page: page, field: field, scroll: find(page, data.scroll) }
    }
    function fullFieldAbove(field, page, bottom) {
        const position = field.mapToItem(page, 0, 0)
        return position.y >= -0.5 && position.y + field.height <= bottom + 0.5
    }
    function test_centre_focused_fields_above_docked_keyboard_data() { return centreCases() }
    function test_centre_focused_fields_above_docked_keyboard(data) {
        const fixture = createCentre(data)
        showKeyboard(230)
        tryVerify(function() { return fullFieldAbove(fixture.field, fixture.page, 303) }, 1000,
            "complete editable field must be above docked keyboard")
        compare(fixture.field.text, "synthetic-draft")
        compare(fixture.scroll.height, 303)
        ime.visible = false
        tryCompare(fixture.scroll, "height", 533)
        compare(fixture.field.text, "synthetic-draft")
    }
    function test_centre_zero_rectangle_and_field_switch_data() { return centreCases() }
    function test_centre_zero_rectangle_and_field_switch(data) {
        const fixture = createCentre(data)
        ime.visible = true
        tryVerify(function() {
            const top = fixture.field.mapToItem(fixture.page, 0, 0).y
            return top >= 0 && top <= 40
        }, 1000, "floating keyboard fallback lifts the field")
        if (data.tag === "security") {
            const first = find(fixture.page, "securityCurrentPassword")
            first.forceActiveFocus(Qt.OtherFocusReason)
            tryVerify(function() { return fullFieldAbove(first, fixture.page, 533) }, 1000)
            fixture.field.forceActiveFocus(Qt.OtherFocusReason)
            tryVerify(function() { return fixture.field.mapToItem(fixture.page, 0, 0).y <= 40 }, 1000)
        }
        compare(fixture.field.text, "synthetic-draft")
        ime.visible = false
        tryCompare(fixture.scroll, "height", 533)
    }
    function test_native_keyboard_top_overrides_stale_qt_rectangle() {
        const frame = createTemporaryObject(signInComponent, surface)
        const viewport = find(frame, "accountKeyboardViewport")
        verify(viewport !== null)
        viewport.keyboardPlatform = nativeKeyboard
        nativeKeyboard.dockedKeyboardTop = 303
        showKeyboard(100)
        tryCompare(frame, "keyboardOverlap", 230)
        nativeKeyboard.dockedKeyboardTop = -1
        tryCompare(frame, "keyboardOverlap", 100)
        ime.visible = false
        tryCompare(frame, "keyboardOverlap", 0)
    }

    function test_security_stacked_fields_reveal_on_keyboard_reopen() {
        testWindow.width = 600
        const fixture = createCentre(centreCases()[1])
        const current = find(fixture.page, "securityCurrentPassword")
        const next = find(fixture.page, "securityNewPassword")
        current.forceActiveFocus(Qt.MouseFocusReason)
        showKeyboard(230)
        tryVerify(function() { return fullFieldAbove(current, fixture.page, 303) }, 1000)
        const fields = [next, fixture.field]
        for (let i = 0; i < fields.length; ++i) {
            ime.visible = false
            tryCompare(fixture.scroll, "height", 533)
            fields[i].forceActiveFocus(Qt.MouseFocusReason)
            showKeyboard(230)
            tryVerify(function() { return fullFieldAbove(fields[i], fixture.page, 303) }, 1000,
                "stacked lower field must reveal on focus/reopen without explicit reveal")
        }
        compare(fixture.field.text, "synthetic-draft")
    }
    function test_native_sampling_reveals_lower_field_despite_stale_qt_rectangle_data() {
        return [{ tag: "regular", nativeTop: 303, pageTop: 0 },
                { tag: "smallViewport", nativeTop: 129, pageTop: 64 }]
    }
    function test_native_sampling_reveals_lower_field_despite_stale_qt_rectangle(data) {
        testWindow.width = 600
        const fixture = createCentre(centreCases()[1])
        fixture.page.anchors.topMargin = data.pageTop
        const sampler = createTemporaryObject(sampledKeyboardComponent, surface)
        sampler.nextTop = data.nativeTop
        find(fixture.page, "accountKeyboardViewport").keyboardPlatform = sampler
        // Qt reports too little obstruction; only a fresh native sample knows the real top.
        showKeyboard(100)
        tryVerify(function() { return fullFieldAbove(fixture.field, fixture.page, data.nativeTop - data.pageTop) }, 1000,
            "native sample must reveal lower field above actual keyboard, not stale Qt rectangle")
        verify(sampler.sampleCalls > 0)
        ime.visible = false
        wait(150)
        const hiddenSamples = sampler.sampleCalls
        wait(150)
        compare(sampler.sampleCalls, hiddenSamples, "sampling stops with hidden IME")
        sampler.nextTop = data.nativeTop - 10
        fixture.field.forceActiveFocus(Qt.MouseFocusReason)
        showKeyboard(100)
        tryVerify(function() { return fullFieldAbove(fixture.field, fixture.page, data.nativeTop - 10 - data.pageTop) }, 1000,
            "keyboard reopen refreshes native top")
        compare(fixture.field.text, "synthetic-draft")
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

    // Android keyboards re-enable Shift after every letter unless auto-uppercase is off.
    function test_username_fields_do_not_auto_capitalise_data() {
        return [{ tag: "signIn", component: signInComponent, field: "accountSignInUsername" },
                { tag: "create", component: createComponent, field: "accountCreateUsername" }]
    }
    function test_username_fields_do_not_auto_capitalise(data) {
        const frame = createTemporaryObject(data.component, surface, { touchInput: true })
        const field = find(frame, data.field)
        verify(field, data.field)
        verify(field.inputMethodHints & Qt.ImhNoAutoUppercase)
        verify(field.inputMethodHints & Qt.ImhPreferLowercase)
    }
}

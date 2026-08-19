// Account Centre Recovery regressions against production QML.
// The one-time secret remains presenter-owned; the Account Centre page never owns a key value.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountRecoveryCentre"
    when: windowShown

    Window {
        id: testWindow
        width: 1180
        height: 900
        visible: true
    }

    Component {
        id: controllerComponent
        QtObject {
            property string mode: "signedIn"
            property string lastErrorMessage: ""
            property int replaceCalls: 0
            property string lastPassword: ""

            signal accountError(string category, string code, string message)
            signal recoveryKeyReplacementSucceeded()
            signal recoveryKeyReplacementFailed(string message)
            signal signedOut()
            signal currentDeviceLocked()

            function replaceRecoveryKey(password) {
                replaceCalls += 1
                lastPassword = password
            }
        }
    }

    Component {
        id: presenterComponent
        QtObject {
            property bool active: false
            property string purpose: ""
            property string recoveryKey: ""
            property string copyState: "idle"
            property int copyCalls: 0
            property int dismissCalls: 0
            property bool copyShouldSucceed: true

            function copyRecoveryKey() {
                copyCalls += 1
                copyState = copyShouldSucceed ? "copied" : "failed"
                return copyShouldSucceed
            }

            function dismiss() {
                dismissCalls += 1
                recoveryKey = ""
                copyState = "idle"
                active = false
            }
        }
    }

    Component {
        id: pageComponent
        Account.AccountRecoveryPage {}
    }

    Component {
        id: keyPageComponent
        Account.AccountRecoveryKey {}
    }

    property var controller: null
    property var presenter: null
    property var page: null

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

    function init() {
        controller = controllerComponent.createObject(testWindow)
        presenter = presenterComponent.createObject(testWindow)
        verify(controller !== null)
        verify(presenter !== null)

        page = pageComponent.createObject(testWindow.contentItem, {
            "width": 980,
            "height": 820,
            "controller": controller,
            "presenter": presenter,
            "active": true
        })
        verify(page !== null)
        testWindow.requestActivate()
        wait(0)
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (controller)
            controller.destroy()
        if (presenter)
            presenter.destroy()
        page = null
        controller = null
        presenter = null
    }

    function test_first_replace_click_only_expands() {
        var replace = byName(page, "recoveryReplaceButton")
        var field = byName(page, "recoveryCurrentPassword")
        verify(replace !== null)
        verify(field !== null)

        mouseClick(replace, replace.width / 2, replace.height / 2)
        wait(0)
        waitForRendering(page)

        compare(controller.replaceCalls, 0)
        compare(page.replaceExpanded, true)
        tryVerify(function() { return field.activeFocus }, 1000)
    }

    function test_cancel_clears_password_and_reveal_without_mutation() {
        page.beginReplacement()
        wait(0)

        var field = byName(page, "recoveryCurrentPassword")
        var cancel = byName(page, "recoveryReplaceCancel")
        verify(field !== null)
        verify(cancel !== null)

        field.text = "fixture current password"
        field.parent.reveal = true
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        wait(0)

        compare(controller.replaceCalls, 0)
        compare(field.text, "")
        compare(field.parent.reveal, false)
        compare(page.replaceExpanded, false)
    }

    function test_generate_requires_password_and_submits_exactly_once() {
        page.beginReplacement()
        wait(0)

        var field = byName(page, "recoveryCurrentPassword")
        var generate = byName(page, "recoveryGenerateButton")
        verify(field !== null)
        verify(generate !== null)

        page.submitReplacement()
        compare(controller.replaceCalls, 0)
        verify(page.validationMessage.length > 0)

        field.text = "  fixture password spaces stay data  "
        page.submitReplacement()
        compare(controller.replaceCalls, 1)
        compare(controller.lastPassword, "  fixture password spaces stay data  ")
        compare(page.requestPending, true)

        page.submitReplacement()
        compare(controller.replaceCalls, 1)
    }

    function test_failure_never_creates_success_state_and_clears_password() {
        page.beginReplacement()
        wait(0)

        var field = byName(page, "recoveryCurrentPassword")
        field.text = "wrong fixture password"
        page.submitReplacement()
        compare(controller.replaceCalls, 1)

        controller.lastErrorMessage = "The current password is incorrect."
        controller.recoveryKeyReplacementFailed(controller.lastErrorMessage)
        wait(0)

        compare(page.requestPending, false)
        compare(page.replaceExpanded, true)
        compare(field.text, "")
        compare(presenter.active, false)
        verify(page.nativeErrorVisible)
    }

    function test_unrelated_global_error_during_pending_request_is_ignored() {
        page.beginReplacement()
        wait(0)

        var field = byName(page, "recoveryCurrentPassword")
        field.text = "fixture password"
        page.submitReplacement()
        compare(page.requestPending, true)

        controller.accountError(
            "offline",
            "unrelated_operation_failed",
            "Another account operation failed.")
        wait(0)

        compare(page.requestPending, true)
        compare(page.replaceExpanded, true)
        compare(field.text, "fixture password")
        compare(page.nativeErrorVisible, false)
    }

    function test_recovery_success_signal_never_substitutes_for_presenter_truth() {
        page.beginReplacement()
        wait(0)

        var field = byName(page, "recoveryCurrentPassword")
        field.text = "fixture password"
        page.submitReplacement()
        verify(page.requestPending)

        controller.recoveryKeyReplacementSucceeded()
        wait(0)

        compare(page.requestPending, false)
        compare(page.replaceExpanded, true)
        compare(page["recoveryKey"], undefined)

        presenter.purpose = "manualReplacement"
        presenter.recoveryKey = "fixture-manual-key"
        presenter.active = true
        wait(0)

        compare(page.replaceExpanded, false)
        compare(field.text, "")
    }

    function test_only_manual_replacement_presenter_activity_completes_form() {
        page.beginReplacement()
        wait(0)
        var field = byName(page, "recoveryCurrentPassword")
        field.text = "fixture password"
        page.submitReplacement()
        verify(page.requestPending)

        presenter.purpose = "accountCreated"
        presenter.recoveryKey = "fixture-non-manual-key"
        presenter.active = true
        wait(0)
        compare(page.replaceExpanded, true)
        compare(page.requestPending, true)

        presenter.active = false
        presenter.purpose = "manualReplacement"
        presenter.recoveryKey = "fixture-manual-key"
        presenter.active = true
        wait(0)

        compare(page.replaceExpanded, false)
        compare(page.requestPending, false)
        compare(field.text, "")
        compare(page["recoveryKey"], undefined)
    }

    function test_navigation_and_signout_clear_password() {
        page.beginReplacement()
        wait(0)
        var field = byName(page, "recoveryCurrentPassword")
        field.text = "leave sentinel"
        field.parent.reveal = true

        page.active = false
        compare(field.text, "")
        compare(field.parent.reveal, false)

        page.active = true
        page.beginReplacement()
        field.text = "signout sentinel"
        controller.signedOut()
        compare(field.text, "")
        compare(page.replaceExpanded, false)
    }

    function test_manual_result_reads_and_mutates_only_through_presenter() {
        presenter.purpose = "manualReplacement"
        presenter.recoveryKey = "CLSM-FIXTURE-MANUAL-KEY"
        presenter.active = true

        var keyPage = keyPageComponent.createObject(testWindow.contentItem, {
            "width": 1080,
            "height": 820,
            "presenter": presenter
        })
        verify(keyPage !== null)
        wait(0)

        compare(keyPage.objectName, "accountManualReplacementRecoveryKey")
        var keyValue = byName(keyPage, "accountRecoveryKeyValue")
        var copy = byName(keyPage, "accountRecoveryKeyCopyManual")
        var done = byName(keyPage, "accountRecoveryKeySavedManual")
        verify(keyValue !== null)
        verify(copy !== null)
        verify(done !== null)
        compare(keyValue.text, "CLSM-FIXTURE-MANUAL-KEY")

        mouseClick(copy, copy.width / 2, copy.height / 2)
        compare(presenter.copyCalls, 1)

        mouseClick(done, done.width / 2, done.height / 2)
        wait(0)
        compare(presenter.dismissCalls, 1)
        compare(presenter.recoveryKey, "")
        compare(keyValue.text, "")

        keyPage.destroy()
    }

    function test_manual_copy_failure_keeps_key_visible() {
        presenter.purpose = "manualReplacement"
        presenter.recoveryKey = "CLSM-FIXTURE-COPY-FAIL"
        presenter.copyShouldSucceed = false
        presenter.active = true

        var keyPage = keyPageComponent.createObject(testWindow.contentItem, {
            "width": 1080,
            "height": 820,
            "presenter": presenter
        })
        verify(keyPage !== null)
        wait(0)

        var keyValue = byName(keyPage, "accountRecoveryKeyValue")
        var copy = byName(keyPage, "accountRecoveryKeyCopyManual")
        mouseClick(copy, copy.width / 2, copy.height / 2)
        wait(0)

        compare(presenter.copyState, "failed")
        compare(keyValue.text, "CLSM-FIXTURE-COPY-FAIL")
        keyPage.destroy()
    }

    function test_narrow_layout_stacks_hero_and_keeps_form_reachable() {
        page.width = 760
        page.beginReplacement()
        wait(0)

        verify(page.compact)
        var hero = byName(page, "recoveryHeroGrid")
        var field = byName(page, "recoveryCurrentPassword")
        var generate = byName(page, "recoveryGenerateButton")
        verify(hero !== null)
        compare(hero.columns, 1)
        verify(field.visible)
        verify(generate.visible)
    }
}

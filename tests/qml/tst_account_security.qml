// Account Centre Security regression tests against the production QML surface.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountSecurity"

    Window {
        id: testWindow
        width: 1100
        height: 900
        visible: true
    }

    Component {
        id: controllerComponent

        QtObject {
            property string mode: "signedIn"
            property bool newDeviceProtection: true
            property bool signOutSyncWarningPending: false
            property string lastErrorMessage: ""

            property int refreshApprovalCalls: 0
            property int protectionCalls: 0
            property bool lastProtectionValue: false
            property int passwordCalls: 0
            property string lastCurrentPassword: ""
            property string lastNewPassword: ""
            property int logoutCalls: 0
            property int approvalDecisionCalls: 0
            property string lastApprovalKind: ""
            property string lastApprovalChallengeId: ""
            property bool lastApprovalDecision: false

            signal approvalRequestsChanged(var requests)
            signal passwordChangeSucceeded()
            signal accountError(string category, string code, string message)
            signal signedOut()
            signal currentDeviceLocked()

            function refreshApprovals() {
                refreshApprovalCalls += 1
            }

            function setNewDeviceProtection(enabled) {
                protectionCalls += 1
                lastProtectionValue = enabled
            }

            function changePassword(currentPassword, newPassword) {
                passwordCalls += 1
                lastCurrentPassword = currentPassword
                lastNewPassword = newPassword
            }

            function logoutEverywhere() {
                logoutCalls += 1
            }

            function decideApproval(kind, challengeId, approve) {
                approvalDecisionCalls += 1
                lastApprovalKind = kind
                lastApprovalChallengeId = challengeId
                lastApprovalDecision = approve
            }
        }
    }

    Component {
        id: pageComponent
        Account.AccountSecurityPage {}
    }

    property var controller: null
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
        verify(controller !== null)

        page = pageComponent.createObject(testWindow.contentItem, {
            "width": 980,
            "height": 820,
            "controller": controller,
            "active": true
        })
        verify(page !== null)
        wait(0)
        compare(controller.refreshApprovalCalls, 1)
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (controller)
            controller.destroy()
        page = null
        controller = null
    }

    function test_protection_waits_for_authoritative_controller_state() {
        compare(controller.newDeviceProtection, true)
        compare(page.protectionRequestPending, false)

        page.requestProtectionToggle()
        compare(controller.protectionCalls, 1)
        compare(controller.lastProtectionValue, false)
        compare(controller.newDeviceProtection, true)
        compare(page.protectionRequestPending, true)

        controller.newDeviceProtection = false
        wait(0)
        compare(page.protectionRequestPending, false)
        compare(controller.newDeviceProtection, false)
    }

    function test_approval_uses_real_identifiers_and_is_not_removed_optimistically() {
        var requests = [
            { "kind": "device_sign_in", "challenge_id": "challenge-a" },
            { "kind": "device_sign_in", "challenge_id": "challenge-b" }
        ]
        controller.approvalRequestsChanged(requests)
        wait(0)

        compare(page.approvalCount, 2)
        compare(byName(page, "securityProtectionRow").visible, false)

        page.requestApproval(requests[0], true)
        compare(controller.approvalDecisionCalls, 1)
        compare(controller.lastApprovalKind, "device_sign_in")
        compare(controller.lastApprovalChallengeId, "challenge-a")
        compare(controller.lastApprovalDecision, true)
        compare(page.approvalCount, 2)

        controller.approvalRequestsChanged([requests[1]])
        wait(0)
        compare(page.approvalCount, 1)
        compare(page.approvalDecisionKey, "")
    }

    function test_missing_approval_identifiers_never_call_controller() {
        var incomplete = { "kind": "device_sign_in" }
        controller.approvalRequestsChanged([incomplete])
        wait(0)

        page.requestApproval(incomplete, true)
        compare(controller.approvalDecisionCalls, 0)
        compare(page.approvalCount, 1)
    }

    function test_password_stays_until_verified_success_then_clears() {
        page.passwordExpanded = true
        wait(0)

        var current = byName(page, "securityCurrentPassword")
        var next = byName(page, "securityNewPassword")
        var confirm = byName(page, "securityConfirmPassword")
        var save = byName(page, "securityPasswordSave")
        verify(current !== null)
        verify(next !== null)
        verify(confirm !== null)
        verify(save !== null)

        current.text = "current sentinel"
        next.text = "short"
        confirm.text = "short"
        wait(0)
        verify(!save.enabled)

        next.text = "new password sentinel"
        confirm.text = "does not match"
        wait(0)
        verify(!save.enabled)

        confirm.text = "new password sentinel"
        wait(0)
        verify(save.enabled)

        page.submitPassword()
        compare(controller.passwordCalls, 1)
        compare(controller.lastCurrentPassword, "current sentinel")
        compare(controller.lastNewPassword, "new password sentinel")
        compare(current.text, "current sentinel")
        compare(next.text, "new password sentinel")
        compare(page.passwordExpanded, true)
        compare(page.passwordRequestPending, true)

        controller.passwordChangeSucceeded()
        wait(0)
        compare(current.text, "")
        compare(next.text, "")
        compare(confirm.text, "")
        compare(page.passwordExpanded, false)
        compare(page.passwordRequestPending, false)
    }

    function test_password_secrets_clear_on_cancel_leave_signout_and_lock() {
        var current = byName(page, "securityCurrentPassword")
        var next = byName(page, "securityNewPassword")
        var confirm = byName(page, "securityConfirmPassword")

        page.passwordExpanded = true
        current.text = "one"
        next.text = "two two two"
        confirm.text = "two two two"
        current.parent.reveal = true
        page.closePasswordEditor()
        compare(current.text, "")
        compare(next.text, "")
        compare(confirm.text, "")
        compare(current.parent.reveal, false)

        page.passwordExpanded = true
        current.text = "leave sentinel"
        page.active = false
        compare(current.text, "")

        page.active = true
        current.text = "signout sentinel"
        controller.signedOut()
        compare(current.text, "")

        page.active = true
        current.text = "lock sentinel"
        controller.currentDeviceLocked()
        compare(current.text, "")
    }

    function test_logout_requires_inline_confirmation() {
        compare(page.logoutConfirmationOpen, false)
        compare(controller.logoutCalls, 0)

        page.logoutConfirmationOpen = true
        compare(controller.logoutCalls, 0)

        page.logoutConfirmationOpen = false
        compare(controller.logoutCalls, 0)

        page.logoutConfirmationOpen = true
        page.confirmLogoutEverywhere()
        compare(controller.logoutCalls, 1)
        compare(page.logoutRequestPending, true)
    }

    function test_compact_width_stacks_password_fields() {
        page.width = 700
        page.passwordExpanded = true
        wait(0)

        verify(page.compactPassword)
        var grid = byName(page, "securityPasswordGrid")
        verify(grid !== null)
        compare(grid.columns, 1)
    }
}

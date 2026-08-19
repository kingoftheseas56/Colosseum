// Account Centre Profile regressions against production QML.
// Draft editing must not be consumed by unrelated global account activity.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountProfileCentre"

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
            property string username: "Hemanth56"
            property string avatarId: "initial"
            property string lastErrorMessage: ""
            property int refreshCalls: 0
            property int renameCalls: 0
            property int avatarCalls: 0
            property string lastRename: ""
            property string lastAvatar: ""

            signal accountError(string category, string code, string message)
            signal usernameRenameSucceeded()
            signal usernameRenameFailed(string message)
            signal builtinAvatarChangeSucceeded()
            signal builtinAvatarChangeFailed(string message)
            signal signedOut()
            signal currentDeviceLocked()

            function refreshProfile() { refreshCalls += 1 }
            function renameUsername(value) {
                renameCalls += 1
                lastRename = value
            }
            function setBuiltinAvatar(value) {
                avatarCalls += 1
                lastAvatar = value
            }
        }
    }

    Component {
        id: pageComponent
        Account.AccountProfilePage {}
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
            "controller": controller
        })
        verify(page !== null)
        wait(0)
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (controller)
            controller.destroy()
        page = null
        controller = null
    }

    function test_unrelated_global_error_does_not_eat_username_draft() {
        var field = byName(page, "accountProfileUsername")
        verify(field !== null)
        field.text = "Unsaved Draft"
        compare(page.usernameDraft, "Unsaved Draft")

        controller.accountError("offline", "unrelated", "Another account operation failed.")
        wait(0)

        compare(page.usernameDraft, "Unsaved Draft")
        compare(page.operationErrorMessage, "")
        compare(page.profileUpdated, false)
    }

    function test_background_username_refresh_preserves_active_unsaved_draft() {
        var field = byName(page, "accountProfileUsername")
        verify(field !== null)
        field.text = "Local Draft"
        field.forceActiveFocus()

        controller.username = "Server Refresh Name"
        wait(0)

        compare(page.usernameDraft, "Local Draft")
        compare(page.profileUpdated, false)
    }

    function test_rename_success_is_operation_specific() {
        var field = byName(page, "accountProfileUsername")
        verify(field !== null)
        field.text = "Hemanth57"

        page.saveUsername()
        compare(controller.renameCalls, 1)
        compare(controller.lastRename, "Hemanth57")
        compare(page.usernameRequestPending, true)

        controller.username = "Hemanth57"
        controller.usernameRenameSucceeded()
        wait(0)

        compare(page.usernameRequestPending, false)
        compare(page.usernameDraft, "Hemanth57")
        compare(page.profileUpdated, true)
        compare(page.operationErrorMessage, "")
    }

    function test_rename_failure_preserves_draft() {
        var field = byName(page, "accountProfileUsername")
        field.text = "Wanted Name"
        page.saveUsername()
        verify(page.usernameRequestPending)

        controller.usernameRenameFailed("That username is unavailable.")
        wait(0)

        compare(page.usernameRequestPending, false)
        compare(page.usernameDraft, "Wanted Name")
        compare(page.operationErrorMessage, "That username is unavailable.")
        compare(page.profileUpdated, false)
    }

    function test_avatar_refresh_is_not_presented_as_user_success() {
        controller.avatarId = "laurel"
        wait(0)
        compare(page.profileUpdated, false)

        page.requestAvatar("book")
        compare(controller.avatarCalls, 1)
        compare(controller.lastAvatar, "book")
        compare(page.avatarRequestPending, true)

        controller.avatarId = "book"
        controller.builtinAvatarChangeSucceeded()
        wait(0)

        compare(page.avatarRequestPending, false)
        compare(page.persistedAvatarId, "book")
        compare(page.profileUpdated, true)
    }

    function test_avatar_failure_is_scoped_and_authoritative_value_stays() {
        controller.avatarId = "column"
        page.requestAvatar("panels")
        compare(page.avatarRequestPending, true)

        controller.builtinAvatarChangeFailed("Profile service unavailable.")
        wait(0)

        compare(page.avatarRequestPending, false)
        compare(page.persistedAvatarId, "column")
        compare(page.operationErrorMessage, "Profile service unavailable.")
        compare(page.profileUpdated, false)
    }

    function test_signed_out_clears_pending_rename_and_toast_state() {
        var field = byName(page, "accountProfileUsername")
        verify(field !== null)
        field.text = "Rename In Flight"
        page.saveUsername()
        verify(page.usernameRequestPending)

        controller.signedOut()
        wait(0)

        compare(page.usernameRequestPending, false)
        compare(page.avatarRequestPending, false)
        compare(page.profileUpdated, false)
        compare(page.operationErrorMessage, "")
    }

    function test_current_device_locked_clears_pending_rename_and_toast_state() {
        var field = byName(page, "accountProfileUsername")
        verify(field !== null)
        field.text = "Rename In Flight Again"
        page.saveUsername()
        verify(page.usernameRequestPending)

        controller.currentDeviceLocked()
        wait(0)

        compare(page.usernameRequestPending, false)
        compare(page.avatarRequestPending, false)
        compare(page.profileUpdated, false)
        compare(page.operationErrorMessage, "")
    }
}

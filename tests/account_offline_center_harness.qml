import QtQuick
import QtQuick.Controls
import "../qml/account" as Account

Item {
    id: harness
    width: 1280
    height: 900
    property var fails: []

    function ok(condition, label) {
        if (!condition)
            fails.push(label)
    }

    function byName(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = byName(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function findText(root, value) {
        if (!root)
            return null
        if (root.text !== undefined && root.text === value)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findText(children[i], value)
            if (found)
                return found
        }
        return null
    }

    function clickTextAction(textItem) {
        if (!textItem)
            return false
        const children = textItem.children || []
        for (let i = 0; i < children.length; ++i) {
            if (children[i].clicked !== undefined) {
                children[i].clicked(null)
                return true
            }
        }
        return false
    }

    QtObject {
        id: fakeController
        property string mode: "offline"
        property string username: "OfflineOwner"
        property string avatarId: "initial"
        property var devices: []
        property bool newDeviceProtection: true
        property bool signOutSyncWarningPending: false
        property string lastErrorMessage: ""
        property int logoutCalls: 0
        property int returnToSignInCalls: 0
        signal approvalRequestsChanged(var requests)
        signal passwordChangeSucceeded()
        signal accountError(string category, string code, string message)
        signal signedOut()
        signal currentDeviceLocked()
        function logoutCurrent() { ++logoutCalls }
        function returnToSignIn() { ++returnToSignInCalls }
        function refreshProfile() {}
        function refreshApprovals() {}
        function refreshDevices() {}
        function deviceId() { return "device-test" }
    }

    Account.AccountCenter {
        id: center
        width: harness.width
        height: harness.height
        controller: fakeController
        initial: "O"
    }

    function runOfflinePhase() {
        center.open("colosseum")
        const username = byName(center, "accountCenterUsername")
        const sessionAction = findText(center, "Sign out")
        ok(username !== null, "Account Centre username selector must exist")
        ok(username && username.text === "OfflineOwner",
           "offline Account Centre must show remembered username")
        ok(sessionAction !== null, "offline Account Centre rail must show Sign out")
        ok(clickTextAction(sessionAction), "offline Account Centre Sign out must be clickable")
        ok(fakeController.logoutCalls === 1,
           "offline Account Centre Sign out must call logoutCurrent once")
        ok(fakeController.returnToSignInCalls === 0,
           "offline Account Centre Sign out must not call returnToSignIn")

        center.open("profile")
        const profilePage = byName(center, "accountProfilePage")
        const usernameField = byName(center, "accountProfileUsername")
        ok(profilePage !== null, "profile page must load")
        ok(usernameField !== null, "profile username field must load")
        if (usernameField)
            usernameField.text = "OfflineRename"
        ok(profilePage && profilePage.saveEnabled === false,
           "profile network mutation must remain disabled offline")

        fakeController.mode = "localOnly"
        center.open("colosseum")
        Qt.callLater(runLocalOnlyPhase)
    }

    function runLocalOnlyPhase() {
        const username = byName(center, "accountCenterUsername")
        ok(username && username.text === "Not signed in",
           "local-only Account Centre header must say Not signed in")
        const signIn = findText(center, "Sign in")
        ok(signIn !== null, "local-only Account Centre rail must show Sign in")
        ok(clickTextAction(signIn), "local-only Account Centre Sign in must be clickable")
        ok(fakeController.logoutCalls === 1,
           "local-only Sign in must not add logout calls")
        ok(fakeController.returnToSignInCalls === 1,
           "local-only Sign in must call returnToSignIn once")

        if (fails.length)
            console.error("ACCOUNT_OFFLINE_CENTER_FAILS:\n  " + fails.join("\n  "))
        else
            console.log("ACCOUNT_OFFLINE_CENTER_OK")
        Qt.exit(fails.length ? 1 : 0)
    }

    Timer {
        interval: 5000
        running: true
        repeat: false
        onTriggered: {
            console.error("ACCOUNT_OFFLINE_CENTER_TIMEOUT")
            Qt.exit(99)
        }
    }

    Component.onCompleted: Qt.callLater(runOfflinePhase)
}

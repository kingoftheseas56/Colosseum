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

    function hasTextContaining(root, fragment) {
        if (!root)
            return false
        if (root.text !== undefined
            && String(root.text).indexOf(fragment) !== -1)
            return true
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            if (hasTextContaining(children[i], fragment))
                return true
        }
        return false
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
        property string localDeviceLabel: "Device 482731"
        property string syncState: "retrying"
        property string restoreStage: "offline"
        property bool onboardingRequired: false
        property var devices: []
        property int deviceCount: 0
        property bool newDeviceProtection: true
        property int pendingOutboxCount: 0
        property bool signOutSyncWarningPending: false
        property string deletionEffectiveAt: ""
        property string errorCategory: "offline"
        property string lastErrorCode: "offline"
        property string lastErrorMessage: ""
        property bool busy: false
        property int logoutCalls: 0
        property int returnToSignInCalls: 0

        signal approvalRequestsChanged(var requests)
        signal accountError(string category, string code, string message)
        signal builtinAvatarChangeFailed(string message)
        signal builtinAvatarChangeSucceeded()
        signal currentDeviceLocked()
        signal deviceListRefreshFailed(string message)
        signal deviceListRefreshSucceeded()
        signal deviceRevokeFailed(string deviceId, string message)
        signal deviceRevokeSucceeded(string deviceId)
        signal passwordChangeSucceeded()
        signal recoveryKeyReplacementFailed(string message)
        signal recoveryKeyReplacementSucceeded()
        signal signedOut()
        signal usernameRenameFailed(string message)
        signal usernameRenameSucceeded()

        function logoutCurrent() { ++logoutCalls }
        function returnToSignIn() { ++returnToSignInCalls }
        function refreshProfile() {}
        function refreshApprovals() {}
        function refreshDevices() {}
        function renameUsername(value) {}
        function setBuiltinAvatar(value) {}
        function changePassword(currentPassword, newPassword) {}
        function replaceRecoveryKey(currentPassword) {}
        function revokeDevice(value) {}
        function setNewDeviceProtection(enabled) {}
        function deviceId() { return "server-device-test" }
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
        ok(clickTextAction(sessionAction),
           "offline Account Centre Sign out must be clickable")
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
        fakeController.username = ""
        center.open("colosseum")
        Qt.callLater(runLocalOnlyPhase)
    }

    function runLocalOnlyPhase() {
        const username = byName(center, "accountCenterUsername")
        ok(username !== null, "local Account Centre header selector must exist")
        ok(username && username.text === "Device 482731",
           "local Account Centre header must display the friendly device label")
        ok(findText(center, "Not signed in") === null,
           "local Account Centre must not render Not signed in")
        ok(center.activeSection === "colosseum",
           "local Account Centre must open Your Colosseum")

        const colosseumRail = byName(center, "accountCenterRail_colosseum")
        const privacyRail = byName(center, "accountCenterRail_privacy")
        ok(colosseumRail !== null && colosseumRail.visible,
           "local Account Centre must show Your Colosseum rail")
        ok(privacyRail !== null && privacyRail.visible,
           "local Account Centre must show Data & privacy rail")
        ok(byName(center, "accountCenterRail_profile") === null,
           "local Account Centre must hide Profile rail")
        ok(byName(center, "accountCenterRail_security") === null,
           "local Account Centre must hide Security rail")
        ok(byName(center, "accountCenterRail_devices") === null,
           "local Account Centre must hide Devices rail")
        ok(byName(center, "accountCenterRail_recovery") === null,
           "local Account Centre must hide Recovery rail")

        const yourColosseum = byName(center, "yourColosseumPage")
        const localBlock = byName(center, "yourColosseumLocalAccountBlock")
        const signIn = byName(center, "yourColosseumLocalSignIn")
        const createAccount = byName(center, "yourColosseumLocalCreateAccount")
        ok(yourColosseum !== null && yourColosseum.visible,
           "Your Colosseum surface must remain present locally")
        ok(localBlock !== null && localBlock.visible,
           "local account block must be visible in Your Colosseum")
        ok(signIn !== null && signIn.visible,
           "local Sign in control must be visible")
        ok(createAccount !== null && createAccount.visible,
           "local Create account control must be visible")

        center.open("privacy")
        const privacyPage = byName(center, "accountDataPrivacyPage")
        const crossDevice = byName(center, "privacyCrossDeviceHistoryGroup")
        const accountData = byName(center, "privacyAccountDataGroup")
        const dangerZone = byName(center, "privacyDangerZone")
        ok(center.activeSection === "privacy",
           "local Account Centre must open Data & privacy")
        ok(privacyPage !== null && privacyPage.visible,
           "local Data & privacy page must be visible")
        ok(crossDevice !== null && !crossDevice.visible,
           "cross-device history group must be hidden locally")
        ok(accountData !== null && !accountData.visible,
           "account data group must be hidden locally")
        ok(dangerZone !== null && !dangerZone.visible,
           "danger zone must be hidden locally")
        ok(hasTextContaining(
               privacyPage,
               "Search history, files, downloads, caches, paths and machine state stay on this device."),
           "local privacy copy must keep search, files, downloads, paths and machine state local")
        ok(hasTextContaining(
               privacyPage,
               "Filesystem paths, local media locations, downloads, caches and other machine-owned state stay on this device."),
           "local privacy copy must keep machine-owned state local")
        ok(hasTextContaining(
               privacyPage,
               "Local files and filesystem locations."),
           "local privacy map must identify local files and filesystem locations")

        center.open("profile")
        ok(center.activeSection === "colosseum",
           "local Account Centre must normalize account-only routes to Your Colosseum")

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

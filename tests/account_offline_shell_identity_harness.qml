import QtQuick
import QtQuick.Controls
import "../qml" as UI
import "../qml/account" as Account

Item {
    id: harness
    width: 1200
    height: 720
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

    function actionText(button) {
        if (!button)
            return ""
        if (button.text !== undefined && button.text.length > 0)
            return button.text
        if (button.contentItem && button.contentItem.text !== undefined)
            return button.contentItem.text
        return ""
    }

    function emitFirstClicked(root) {
        if (!root)
            return false
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            if (children[i].clicked !== undefined) {
                children[i].clicked(null)
                return true
            }
            if (emitFirstClicked(children[i]))
                return true
        }
        return false
    }

    QtObject {
        id: fakeController
        property string mode: "offline"
        property string username: "OfflineOwner"
        property string localDeviceLabel: "Device 482731"
        property string syncState: "retrying"
        property int pendingOutboxCount: 0
        property int logoutCalls: 0
        property int returnToSignInCalls: 0
        function logoutCurrent() { ++logoutCalls }
        function returnToSignIn() { ++returnToSignInCalls }
    }

    Item {
        id: backdrop
        anchors.fill: parent
    }

    UI.TopBar {
        id: topBar
        width: harness.width
        height: 56
        backdrop: backdrop
        accountController: fakeController
        onAccountClicked: function(anchorRight, anchorBottom) {
            flyout.toggleAt(anchorRight, anchorBottom)
        }
    }

    Account.AccountFlyout {
        id: flyout
        width: harness.width
        height: harness.height
        controller: fakeController
        initial: "O"
    }

    function runOfflinePhase() {
        flyout.open()
        const accountButton = byName(topBar, "colosseumTopbarAccountButton")
        const username = byName(flyout, "accountFlyoutUsername")
        const sessionAction = byName(flyout, "accountFlyoutSessionAction")
        ok(accountButton !== null, "top-bar account selector must exist")
        ok(accountButton && accountButton.Accessible.name === "Account: OfflineOwner",
           "offline top bar must expose remembered identity")
        ok(username !== null, "flyout username selector must exist")
        ok(username && username.text === "OfflineOwner",
           "offline flyout must show remembered username")
        ok(sessionAction !== null, "flyout session action selector must exist")
        ok(actionText(sessionAction) === "Sign out",
           "offline flyout session action must be Sign out")
        if (sessionAction)
            sessionAction.clicked()
        ok(fakeController.logoutCalls === 1,
           "offline Sign out must call logoutCurrent exactly once")
        ok(fakeController.returnToSignInCalls === 0,
           "offline Sign out must not call returnToSignIn")

        flyout.close()
        fakeController.mode = "localOnly"
        fakeController.username = ""
        Qt.callLater(runLocalOnlyPhase)
    }

    function runLocalOnlyPhase() {
        const accountButton = byName(topBar, "colosseumTopbarAccountButton")
        const deviceText = byName(topBar, "colosseumTopbarDeviceLabel")
        ok(accountButton !== null, "local top-bar account selector must exist")
        ok(accountButton && accountButton.Accessible.name === "Device",
           "local top-bar Accessible.name must be generic Device")
        ok(deviceText !== null, "local top-bar Device text selector must exist")
        ok(deviceText && deviceText.visible,
           "local top-bar Device text must be visible")
        ok(deviceText && deviceText.text === "Device",
           "local top-bar visible text must be exactly Device")
        ok(!flyout.visible, "local flyout must start closed")

        ok(emitFirstClicked(accountButton),
           "local top-bar Device control must expose a clickable child")
        ok(flyout.visible,
           "clicking Device must open AccountFlyout")

        const identity = byName(flyout, "accountFlyoutLocalIdentity")
        const localLabel = byName(flyout, "accountFlyoutLocalDeviceLabel")
        const yourColosseum = byName(flyout, "accountFlyoutLocalYourColosseum")
        const privacy = byName(flyout, "accountFlyoutLocalPrivacy")
        const signIn = byName(flyout, "accountFlyoutLocalSignIn")
        const createAccount = byName(flyout, "accountFlyoutLocalCreateAccount")

        ok(identity !== null && identity.visible,
           "local flyout identity block must be visible")
        ok(localLabel !== null && localLabel.visible,
           "local flyout device label must be visible")
        ok(localLabel && localLabel.text === "Device 482731",
           "local flyout must show the friendly device identity")
        ok(localLabel && /^Device [0-9]{6}$/.test(localLabel.text),
           "local flyout identity must remain Device plus six digits")
        ok(yourColosseum !== null && yourColosseum.visible,
           "local flyout must expose Your Colosseum")
        ok(privacy !== null && privacy.visible,
           "local flyout must expose Data & privacy")
        ok(signIn !== null && signIn.visible,
           "local flyout must expose Sign in")
        ok(createAccount !== null && createAccount.visible,
           "local flyout must expose Create account")
        ok(actionText(yourColosseum) === "Your Colosseum",
           "local flyout Your Colosseum text must stay stable")
        ok(actionText(privacy) === "Data & privacy",
           "local flyout Data & privacy text must stay stable")
        ok(actionText(signIn) === "Sign in",
           "local flyout Sign in text must stay stable")
        ok(actionText(createAccount) === "Create account",
           "local flyout Create account text must stay stable")

        if (fails.length)
            console.error("ACCOUNT_OFFLINE_SHELL_FAILS:\n  " + fails.join("\n  "))
        else
            console.log("ACCOUNT_OFFLINE_SHELL_OK")
        Qt.exit(fails.length ? 1 : 0)
    }

    Component.onCompleted: Qt.callLater(runOfflinePhase)
}

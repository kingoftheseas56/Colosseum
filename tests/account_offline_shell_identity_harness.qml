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

    QtObject {
        id: fakeController
        property string mode: "offline"
        property string username: "OfflineOwner"
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
    }

    Account.AccountFlyout {
        id: flyout
        width: harness.width
        height: harness.height
        controller: fakeController
        initial: "O"
    }

    function configureTopBarController() {
        try {
            topBar.accountController = fakeController
            return topBar.accountController === fakeController
        } catch (error) {
            fails.push("TopBar must expose injectable accountController: " + error)
            return false
        }
    }

    function runOfflinePhase() {
        configureTopBarController()
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

        fakeController.mode = "localOnly"
        Qt.callLater(runLocalOnlyPhase)
    }

    function runLocalOnlyPhase() {
        const accountButton = byName(topBar, "colosseumTopbarAccountButton")
        const username = byName(flyout, "accountFlyoutUsername")
        const sessionAction = byName(flyout, "accountFlyoutSessionAction")
        ok(accountButton && accountButton.Accessible.name === "Account",
           "local-only top bar must remain signed-out presentation")
        ok(username && username.text === "Not signed in",
           "local-only flyout must say Not signed in")
        ok(actionText(sessionAction) === "Sign in",
           "local-only flyout action must be Sign in")
        if (sessionAction)
            sessionAction.clicked()
        ok(fakeController.logoutCalls === 1,
           "local-only Sign in must not add logout calls")
        ok(fakeController.returnToSignInCalls === 1,
           "local-only Sign in must call returnToSignIn exactly once")

        if (fails.length)
            console.error("ACCOUNT_OFFLINE_SHELL_FAILS:\n  " + fails.join("\n  "))
        else
            console.log("ACCOUNT_OFFLINE_SHELL_OK")
        Qt.exit(fails.length ? 1 : 0)
    }

    Component.onCompleted: Qt.callLater(runOfflinePhase)
}

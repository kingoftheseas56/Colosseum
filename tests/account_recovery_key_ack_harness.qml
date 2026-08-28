import QtQuick
import "../qml/account" as Account

Item {
    id: harness
    width: 900
    height: 760
    property var fails: []
    property string lastFinishedPurpose: ""

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

    function countByName(root, name) {
        if (!root)
            return 0
        let count = root.objectName === name ? 1 : 0
        const children = root.children || []
        for (let i = 0; i < children.length; ++i)
            count += countByName(children[i], name)
        return count
    }

    QtObject {
        id: fakePresenter
        property string purpose: "accountCreated"
        property string recoveryKey: "once-only-recovery-key"
        property string copyState: "idle"
        property int copyCalls: 0
        property int dismissCalls: 0
        function copyRecoveryKey() {
            ++copyCalls
            return true
        }
        function dismiss() { ++dismissCalls }
    }

    Account.AccountRecoveryKey {
        id: page
        width: harness.width
        height: harness.height
        presenter: fakePresenter
        onFinished: function(purpose) {
            harness.lastFinishedPurpose = purpose
        }
    }

    function verifyPurpose(purpose) {
        fakePresenter.purpose = purpose
        lastFinishedPurpose = ""
        const copyBefore = fakePresenter.copyCalls
        const dismissBefore = fakePresenter.dismissCalls
        const keyValue = byName(page, "accountRecoveryKeyValue")
        const copyAction = byName(page, "accountRecoveryKeyCopy")
        const savedAction = byName(page, "accountRecoveryKeySaved")

        ok(keyValue && keyValue.text === "once-only-recovery-key",
           purpose + ": recovery key must be visible")
        ok(countByName(page, "accountRecoveryKeyCopy") === 1,
           purpose + ": exactly one stable copy selector must exist")
        ok(copyAction && copyAction.visible,
           purpose + ": copy action must be visible")
        ok(countByName(page, "accountRecoveryKeySaved") === 1,
           purpose + ": exactly one stable saved selector must exist")
        ok(savedAction && savedAction.visible && savedAction.text === "I saved it",
           purpose + ": finishing action must read I saved it")
        ok(byName(page, "accountRecoveryKeyContinue") === null,
           purpose + ": legacy Continue completion must not exist")
        ok(byName(page, "accountRecoveryKeyCopyManual") === null,
           purpose + ": legacy manual copy selector must not exist")
        ok(byName(page, "accountRecoveryKeySavedManual") === null,
           purpose + ": legacy manual saved selector must not exist")

        if (copyAction)
            copyAction.clicked()
        ok(fakePresenter.copyCalls === copyBefore + 1,
           purpose + ": Copy must call copyRecoveryKey once")
        ok(fakePresenter.dismissCalls === dismissBefore,
           purpose + ": Copy must not dismiss the key")
        ok(lastFinishedPurpose === "",
           purpose + ": Copy must not finish the presentation")

        if (savedAction)
            savedAction.clicked()
        ok(fakePresenter.dismissCalls === dismissBefore + 1,
           purpose + ": I saved it must dismiss exactly once")
        ok(lastFinishedPurpose === purpose,
           purpose + ": finished signal must preserve purpose")
    }

    function run() {
        const purposes = [
            "accountCreated",
            "passwordRecovered",
            "deviceChallengeRecovered",
            "manualReplacement"
        ]
        for (let i = 0; i < purposes.length; ++i)
            verifyPurpose(purposes[i])

        if (fails.length)
            console.error("ACCOUNT_RECOVERY_KEY_ACK_FAILS:\n  " + fails.join("\n  "))
        else
            console.log("ACCOUNT_RECOVERY_KEY_ACK_OK")
        Qt.exit(fails.length ? 1 : 0)
    }

    Component.onCompleted: Qt.callLater(run)
}

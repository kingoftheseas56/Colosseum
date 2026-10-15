import QtQuick
import "../qml/account" as Account

Item {
    id: harness
    width: 900
    height: 700
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

    function repeated(value, count) {
        let out = ""
        for (let i = 0; i < count; ++i)
            out += value
        return out
    }

    QtObject {
        id: fakeController
        property bool busy: false
        property string lastErrorMessage: ""
        property int recoverCalls: 0
        property string lastUsername: ""
        property string lastRecoveryKey: ""
        property string lastPassword: ""
        function recoverPassword(username, recoveryKey, password) {
            ++recoverCalls
            lastUsername = username
            lastRecoveryKey = recoveryKey
            lastPassword = password
        }
    }

    Account.AccountRecovery {
        id: recovery
        width: harness.width
        height: harness.height
        controller: fakeController
    }

    function setForm(password) {
        byName(recovery, "accountRecoveryUsername").text = "RecoveryOwner"
        byName(recovery, "accountRecoveryKeyInput").text = "recovery-key-test"
        byName(recovery, "accountRecoveryNewPassword").text = password
        byName(recovery, "accountRecoveryConfirmPassword").text = password
    }

    function run() {
        const validEight = "A9!b2@c3"
        setForm(validEight)
        recovery.submit()
        ok(fakeController.recoverCalls === 1,
           "exactly 8 code points must be accepted")
        ok(fakeController.lastUsername === "RecoveryOwner",
           "accepted recovery must preserve username")
        ok(fakeController.lastRecoveryKey === "recovery-key-test",
           "accepted recovery must preserve recovery key")
        ok(fakeController.lastPassword === validEight,
           "accepted recovery must preserve exact password")

        setForm("A9!b2@c")
        recovery.submit()
        ok(fakeController.recoverCalls === 1,
           "7 code points must be rejected")

        setForm(repeated("x", 129))
        recovery.submit()
        ok(fakeController.recoverCalls === 1,
           "129 code points must be rejected")

        const unicodeEight = "A😀B😀C😀D😀"
        ok(recovery.passwordCodePoints(unicodeEight) === 8,
           "password policy must count Unicode code points, not UTF-16 units")

        if (fails.length)
            console.error("ACCOUNT_RECOVERY_POLICY_FAILS:\n  " + fails.join("\n  "))
        else
            console.log("ACCOUNT_RECOVERY_POLICY_OK")
        Qt.exit(fails.length ? 1 : 0)
    }

    Component.onCompleted: Qt.callLater(run)
}

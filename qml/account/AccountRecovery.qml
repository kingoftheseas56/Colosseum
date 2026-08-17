// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountRecovery"

    property var controller: null
    property string validationMessage: ""

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: "Recover your account."
    detail: "Use your username and recovery key to set a new password."
    panelWidth: 560

    signal signInRequested()

    function passwordCodePoints(value) {
        let count = 0
        for (let i = 0; i < value.length; ++i) {
            const first = value.charCodeAt(i)
            if (first >= 0xD800 && first <= 0xDBFF
                && i + 1 < value.length) {
                const second = value.charCodeAt(i + 1)
                if (second >= 0xDC00 && second <= 0xDFFF)
                    ++i
            }
            ++count
        }
        return count
    }

    function clearSecrets() {
        recoveryKeyField.clear()
        passwordField.clear()
        confirmField.clear()
    }

    function submit() {
        validationMessage = ""
        const username = usernameField.text.trim()
        if (username.length === 0 || recoveryKeyField.text.trim().length === 0) {
            validationMessage = "Enter your username and recovery key."
            return
        }

        const count = passwordCodePoints(passwordField.text)
        if (count < 15 || count > 128) {
            validationMessage = "Use a password between 15 and 128 characters."
            return
        }

        if (passwordField.text !== confirmField.text) {
            validationMessage = "Those passwords do not match."
            return
        }

        if (controller) {
            controller.recoverPassword(
                username,
                recoveryKeyField.text.trim(),
                passwordField.text)
        }
    }

    AccountPanelHeader {
        kicker: "ACCOUNT RECOVERY"
        title: "Reset your password"
    }

    Item { width: 1; height: 24 }

    AccountField {
        id: usernameField
        width: parent.width
        label: "Username"
        placeholderText: "Your username"
        controlObjectName: "accountRecoveryUsername"
        maximumLength: 24
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: recoveryKeyField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: recoveryKeyField
        width: parent.width
        label: "Recovery key"
        placeholderText: "Enter your recovery key"
        controlObjectName: "accountRecoveryKeyInput"
        maximumLength: 128
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: passwordField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: passwordField
        width: parent.width
        label: "New password"
        placeholderText: "Create a new password"
        controlObjectName: "accountRecoveryNewPassword"
        password: true
        maximumLength: 512
        onAccepted: confirmField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: confirmField
        width: parent.width
        label: "Confirm new password"
        placeholderText: "Enter it again"
        controlObjectName: "accountRecoveryConfirmPassword"
        password: true
        maximumLength: 512
        onAccepted: root.submit()
    }

    Text {
        width: parent.width
        visible: root.validationMessage.length > 0
            || (root.controller
                && root.controller.lastErrorMessage
                && root.controller.lastErrorMessage.length > 0)
        text: root.validationMessage.length > 0
            ? root.validationMessage
            : root.controller.lastErrorMessage
        color: "#f0a3a3"
        font.family: errorTheme.ui
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Theme { id: errorTheme }
    }

    Item { width: 1; height: 16 }

    AccountButton {
        objectName: "accountRecoverySubmit"
        width: parent.width
        text: root.controller && root.controller.busy ? "Resetting password…" : "Reset password"
        variant: "primary"
        enabled: !root.controller || !root.controller.busy
        onClicked: root.submit()
    }

    Item { width: 1; height: 10 }

    AccountButton {
        objectName: "accountRecoveryBackToSignIn"
        width: parent.width
        text: "Back to sign in"
        variant: "link"
        onClicked: {
            root.clearSecrets()
            root.signInRequested()
        }
    }
}

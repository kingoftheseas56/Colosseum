// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountCreate"

    property var controller: null
    property string validationMessage: ""

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: "Create your account."
    detail: "Keep your Collection, progress, reader setup, preferences, and other personal Colosseum state with you across devices."
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

    function usernameValid(value) {
        return /^[A-Za-z0-9](?:[A-Za-z0-9_]{1,22}[A-Za-z0-9])$/.test(value)
    }

    function clearSecrets() {
        passwordField.clear()
        confirmField.clear()
    }

    function submit() {
        validationMessage = ""
        const username = usernameField.text.trim()

        if (!usernameValid(username)) {
            validationMessage = "Use 3–24 letters, numbers, or underscores, with a letter or number at both ends."
            return
        }

        const count = passwordCodePoints(passwordField.text)
        if (count < 8 || count > 128) {
            validationMessage = "Use a password of 8–128 characters."
            return
        }

        if (passwordField.text !== confirmField.text) {
            validationMessage = "Those passwords do not match."
            return
        }

        if (controller)
            controller.createAccount(username, passwordField.text)
    }

    AccountPanelHeader {
        kicker: "CREATE ACCOUNT"
        title: "Choose your login"
        copy: "No email or phone number required."
    }

    Item { width: 1; height: 24 }

    AccountField {
        id: usernameField
        width: parent.width
        label: "Username"
        hint: "This is also your Colosseum handle."
        placeholderText: "Choose a username"
        controlObjectName: "accountCreateUsername"
        maximumLength: 24
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: passwordField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: passwordField
        width: parent.width
        label: "Password"
        placeholderText: "Create a password"
        controlObjectName: "accountCreatePassword"
        password: true
        maximumLength: 512
        onAccepted: confirmField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: confirmField
        width: parent.width
        label: "Confirm password"
        placeholderText: "Enter it again"
        controlObjectName: "accountCreateConfirmPassword"
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
        font.family: validationTheme.ui
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Theme { id: validationTheme }
    }

    Item { width: 1; height: 16 }

    AccountButton {
        objectName: "accountCreateSubmit"
        width: parent.width
        text: root.controller && root.controller.busy ? "Creating account…" : "Create account"
        variant: "primary"
        enabled: !root.controller || !root.controller.busy
        onClicked: root.submit()
    }

    Item { width: 1; height: 18 }

    Text {
        width: parent.width
        text: "or"
        color: dividerTheme.inkDimmer
        font.family: dividerTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        Theme { id: dividerTheme }
    }

    Item { width: 1; height: 14 }

    AccountButton {
        objectName: "accountCreateSignIn"
        width: parent.width
        text: "Already have an account? Sign in"
        onClicked: {
            root.clearSecrets()
            root.signInRequested()
        }
    }

    Item { width: 1; height: 10 }

    AccountButton {
        objectName: "accountCreateContinueLocal"
        width: parent.width
        text: "Continue without account"
        variant: "link"
        onClicked: {
            root.clearSecrets()
            if (root.controller)
                root.controller.continueWithoutAccount()
        }
    }

    Item { width: 1; height: 20 }

    Text {
        width: parent.width
        text: "Your recovery key is created after your account is created."
        color: footerTheme.inkDimmer
        font.family: footerTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Theme { id: footerTheme }
    }
}

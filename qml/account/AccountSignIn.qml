// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountSignIn"

    property var controller: null
    property string validationMessage: ""

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: "Welcome back."
    detail: "Sign in to restore your Collection, progress, reader setup, preferences, and other synced Colosseum state on this device."
    panelWidth: 560

    signal forgotPasswordRequested()
    signal createAccountRequested()

    function clearSecrets() {
        passwordField.clear()
    }

    function submit() {
        validationMessage = ""
        const username = usernameField.text.trim()
        if (username.length === 0 || passwordField.text.length === 0) {
            validationMessage = "Enter your username and password."
            return
        }
        if (controller)
            controller.signIn(username, passwordField.text)
    }

    AccountPanelHeader {
        kicker: "SIGN IN"
        title: "Enter your account"
        copy: "Use your Colosseum username and password."
    }

    Item { width: 1; height: 24 }

    AccountField {
        id: usernameField
        width: parent.width
        label: "Username"
        placeholderText: "Your username"
        controlObjectName: "accountSignInUsername"
        maximumLength: 24
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: passwordField.forceInputFocus()
    }

    Item { width: 1; height: 16 }

    AccountField {
        id: passwordField
        width: parent.width
        label: "Password"
        placeholderText: "Your password"
        controlObjectName: "accountSignInPassword"
        password: true
        maximumLength: 512
        onAccepted: root.submit()
    }

    Item { width: 1; height: 10 }

    AccountButton {
        objectName: "accountSignInForgot"
        text: "Forgot password?"
        variant: "link"
        implicitWidth: contentItem.implicitWidth + 16
        onClicked: {
            root.clearSecrets()
            root.forgotPasswordRequested()
        }
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

    Item { width: 1; height: 12 }

    AccountButton {
        objectName: "accountSignInSubmit"
        width: parent.width
        text: root.controller && root.controller.busy ? "Signing in…" : "Sign in"
        variant: "primary"
        enabled: !root.controller || !root.controller.busy
        onClicked: root.submit()
    }

    Item { width: 1; height: 18 }

    Text {
        width: parent.width
        text: "or"
        color: orTheme.inkDimmer
        font.family: orTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        Theme { id: orTheme }
    }

    Item { width: 1; height: 14 }

    AccountButton {
        objectName: "accountSignInCreate"
        width: parent.width
        text: "Create account"
        onClicked: {
            root.clearSecrets()
            root.createAccountRequested()
        }
    }

    Item { width: 1; height: 10 }

    AccountButton {
        objectName: "accountSignInContinueLocal"
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
        text: "No email or phone number required."
        color: footerTheme.inkDimmer
        font.family: footerTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        Theme { id: footerTheme }
    }
}

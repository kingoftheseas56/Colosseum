// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountWelcome"

    property var controller: null

    // No-account edition: sign-in and registration entry points are hidden;
    // the page still renders so a local-storage failure (mode "error") has a
    // visible, retryable surface instead of a silently hidden one.
    readonly property bool accountEntryAvailable:
        !root.controller
        || root.controller.accountFeaturesAvailable !== false

    eyebrow: "COLOSSEUM · WELCOME"
    headline: "Welcome to Colosseum."
    detail: "Use Colosseum completely locally, or sign in to carry your Collection, progress, reader setup, preferences, and other personal choices between devices."
    panelWidth: 560

    signal signInRequested()
    signal createAccountRequested()

    AccountPanelHeader {
        kicker: "START"
        title: "How do you want to begin?"
        copy: "An account is optional. You can change your mind later."
    }

    Item { width: 1; height: 26 }

    AccountChoice {
        objectName: "accountWelcomeContinueLocal"
        width: parent.width
        title: "Continue without account"
        detail: "Keep everything local on this device."
        onChosen: if (root.controller) root.controller.continueWithoutAccount()
    }

    Item { width: 1; height: 10 }

    AccountChoice {
        objectName: "accountWelcomeSignIn"
        width: parent.width
        visible: root.accountEntryAvailable
        title: "Sign in"
        detail: "Bring your Colosseum back on this device."
        onChosen: root.signInRequested()
    }

    Item { width: 1; height: 10 }

    AccountChoice {
        objectName: "accountWelcomeCreateAccount"
        width: parent.width
        visible: root.accountEntryAvailable
        title: "Create account"
        detail: "Start syncing your personal Colosseum."
        onChosen: root.createAccountRequested()
    }

    Item { width: 1; height: 20 }

    Text {
        width: parent.width
        text: root.accountEntryAvailable
            ? "Accounts are optional. Sign in later whenever you want sync."
            : "Colosseum runs completely locally on this device."
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 11
        wrapMode: Text.WordWrap

        Theme { id: theme }
    }
}

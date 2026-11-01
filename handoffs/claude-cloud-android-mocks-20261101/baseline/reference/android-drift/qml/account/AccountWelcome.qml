// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

SettingsScaffold {
    id: welcome
    objectName: "accountWelcome"

    property var controller: null
    property Item backdrop: null

    readonly property bool narrowPhone:layout.phoneLayout

    readonly property bool accountEntryAvailable:
        !welcome.controller || welcome.controller.accountFeaturesAvailable !== false

    signal signInRequested()
    signal createAccountRequested()

    Loader {
        anchors.fill: parent
        active: !welcome.narrowPhone
        sourceComponent: desktopPage
    }

    Loader {
        anchors.fill: parent
        active: welcome.narrowPhone
        sourceComponent: phonePage
    }

    Component {
        id: phonePage
        AccountWelcomePhone {
            controller: welcome.controller
            backdrop: welcome.backdrop
            onSignInRequested: welcome.signInRequested()
            onCreateAccountRequested: welcome.createAccountRequested()
        }
    }

    Component {
        id: desktopPage
        AccountPageFrame {
            id: desktopFrame
            objectName: "accountWelcomeDesktopFrame"
            backdrop: welcome.backdrop
            eyebrow: "COLOSSEUM · WELCOME"
            headline: "Welcome to Colosseum."
            detail: "Use Colosseum completely locally, or sign in to carry your Collection, progress, reader setup, preferences, and other personal choices between devices."
            panelWidth: 560

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
                onChosen: if (welcome.controller) welcome.controller.continueWithoutAccount()
            }

            Item { width: 1; height: 10 }

            AccountChoice {
                objectName: "accountWelcomeSignIn"
                visible: welcome.accountEntryAvailable
                width: parent.width
                title: "Sign in"
                detail: "Bring your Colosseum back on this device."
                onChosen: welcome.signInRequested()
            }

            Item { width: 1; height: 10 }

            AccountChoice {
                objectName: "accountWelcomeCreateAccount"
                visible: welcome.accountEntryAvailable
                width: parent.width
                title: "Create account"
                detail: "Start syncing your personal Colosseum."
                onChosen: welcome.createAccountRequested()
            }

            Item { width: 1; height: 20 }

            Text {
                width: parent.width
                text: welcome.accountEntryAvailable
                    ? "Accounts are optional. Sign in later whenever you want sync."
                    : "Colosseum runs completely locally on this device."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: theme.fontSize(11)
                wrapMode: Text.WordWrap

                Theme { id: theme }
            }
        }
    }
}

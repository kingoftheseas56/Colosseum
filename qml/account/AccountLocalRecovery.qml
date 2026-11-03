// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

// The no-account edition's storage-failure surface. Reachable only through
// the edition's error routing (AccountOnboarding routes mode "error" here
// when accountFeaturesAvailable === false): it shows the useful storage
// error and offers the local retry. No sign-in, registration or account
// recovery action is reachable from this page.
AccountPageFrame {
    id: root
    objectName: "accountLocalRecovery"

    property var controller: null

    readonly property string storageErrorMessage:
        controller && controller.lastErrorMessage
            ? controller.lastErrorMessage
            : "Local-only personal state could not be prepared safely."
    readonly property string storageErrorCode:
        controller && controller.lastErrorCode
            ? controller.lastErrorCode
            : ""

    eyebrow: "COLOSSEUM · LOCAL DATA"
    headline: "Colosseum can't open your local data."
    detail: storageErrorMessage
    panelWidth: 560

    AccountPanelHeader {
        kicker: "RETRY"
        title: "Your library is safe on this device."
        copy: "Nothing was deleted. Colosseum keeps every profile, list and "
              + "preference locally; it just could not open them this time. "
              + "Close anything that may be locking the app's data folder, "
              + "then try again."
    }

    Item { width: 1; height: 26 }

    AccountChoice {
        objectName: "accountLocalRetry"
        width: parent.width
        title: "Try again"
        detail: "Open my local Colosseum and continue without an account."
        onChosen: if (root.controller) root.controller.continueWithoutAccount()
    }

    Item { width: 1; height: 20 }

    Text {
        width: parent.width
        text: root.storageErrorCode.length > 0
            ? qsTr("Diagnostic code: %1").arg(root.storageErrorCode)
            : ""
        visible: root.storageErrorCode.length > 0
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: theme.fontSize(11)
        wrapMode: Text.WordWrap

        Theme { id: theme }
    }
}

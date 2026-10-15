// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountDeviceApproval"

    property var controller: null
    property string validationMessage: ""

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: "Approve this device."
    detail: "A signed-in Colosseum device can approve this sign-in."
    panelWidth: 560

    signal signInRequested()

    function clearSecrets() {
        recoveryField.clear()
    }

    function useRecoveryKey() {
        validationMessage = ""
        const recoveryKey = recoveryField.text.trim()
        if (recoveryKey.length === 0) {
            validationMessage = "Enter your recovery key."
            return
        }

        if (controller)
            controller.useRecoveryKeyForPendingDevice(recoveryKey)

        recoveryField.clear()
    }

    AccountPanelHeader {
        kicker: "NEW DEVICE"
        title: "Waiting for approval"
        copy: "Approve this device from another signed-in Colosseum desktop, or use your recovery key."
    }

    Item { width: 1; height: 24 }

    Rectangle {
        width: parent.width
        height: 58
        radius: 14
        color: Qt.rgba(1, 1, 1, 0.045)
        border.width: 1
        border.color: waitingTheme.edge

        Theme { id: waitingTheme }

        Row {
            anchors.centerIn: parent
            spacing: 10

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: waitingTheme.gold

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 650 }
                    NumberAnimation { to: 1.0; duration: 650 }
                }
            }

            Text {
                text: "Waiting for another device…"
                color: waitingTheme.inkDim
                font.family: waitingTheme.ui
                font.pixelSize: 12
            }
        }
    }

    Item { width: 1; height: 18 }

    AccountButton {
        objectName: "accountDeviceApprovalPoll"
        width: parent.width
        text: "Check again"
        enabled: root.controller
            && root.controller.mode === "awaitingDeviceApproval"
        onClicked: {
            if (root.controller)
                root.controller.pollPendingChallenge()
        }
    }

    Item { width: 1; height: 24 }

    AccountField {
        id: recoveryField
        width: parent.width
        label: "Recovery key"
        placeholderText: "Enter your recovery key"
        controlObjectName: "accountDeviceApprovalRecoveryKey"
        maximumLength: 128
        inputMethodHints: Qt.ImhNoPredictiveText
        onAccepted: root.useRecoveryKey()
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

    Item { width: 1; height: 14 }

    AccountButton {
        objectName: "accountDeviceApprovalUseRecoveryKey"
        width: parent.width
        text: "Use recovery key"
        variant: "primary"
        enabled: root.controller
            && root.controller.mode === "awaitingDeviceApproval"
        onClicked: root.useRecoveryKey()
    }

    Item { width: 1; height: 10 }

    AccountButton {
        objectName: "accountDeviceApprovalBack"
        width: parent.width
        text: "Back to sign in"
        variant: "link"
        onClicked: {
            root.clearSecrets()
            if (root.controller)
                root.controller.cancelPendingAuthentication()
            root.signInRequested()
        }
    }
}

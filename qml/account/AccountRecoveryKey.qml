// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    readonly property bool manualReplacement: purpose === "manualReplacement"

    objectName: manualReplacement
        ? "accountManualReplacementRecoveryKey"
        : (purpose === "passwordRecovered"
        ? "accountRecoverySuccess"
        : (purpose === "deviceChallengeRecovered"
            ? "accountDeviceRecoveryKey"
            : "accountRecoveryKeyIssue"))

    property var presenter: null
    property string purpose: presenter ? presenter.purpose : ""

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: manualReplacement
        ? "Recovery"
        : (purpose === "passwordRecovered"
        ? "Recover your account."
        : (purpose === "deviceChallengeRecovered"
            ? "Save your new recovery key."
            : "Save your recovery key."))
    detail: manualReplacement
        ? "Your account has a new one-time recovery key."
        : (purpose === "passwordRecovered"
        ? "Use your username and recovery key to set a new password."
        : (purpose === "deviceChallengeRecovered"
            ? "The recovery key you used to approve this device has been replaced."
            : "This key can reset your password if you ever lose access."))
    panelWidth: manualReplacement ? 680 : 560

    signal finished(string purpose)

    function copyPresentedKey() {
        if (root.presenter && root.presenter.copyRecoveryKey()) {
            copyToast.visible = true
            copyToastTimer.restart()
        }
    }

    function finishPresentedKey() {
        const completedPurpose = root.purpose
        if (root.presenter)
            root.presenter.dismiss()
        root.finished(completedPurpose)
    }

    Text {
        visible: root.purpose === "passwordRecovered"
        width: parent.width
        text: "✓"
        color: successTheme.gold
        font.family: successTheme.ui
        font.pixelSize: 28
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter

        Theme { id: successTheme }
    }

    Item {
        width: 1
        height: root.purpose === "passwordRecovered" ? 12 : 0
    }

    AccountPanelHeader {
        kicker: root.manualReplacement
            ? "RECOVERY KEY REPLACED"
            : (root.purpose === "passwordRecovered"
            ? "ACCOUNT RECOVERED"
            : (root.purpose === "deviceChallengeRecovered"
                ? "DEVICE APPROVED"
                : "RECOVERY KEY"))
        title: root.manualReplacement
            ? "Save the new key now."
            : (root.purpose === "passwordRecovered"
            ? "Password reset."
            : (root.purpose === "deviceChallengeRecovered"
                ? "Keep the replacement key safe"
                : "Keep this somewhere safe"))
        copy: root.manualReplacement
            ? "The previous recovery key no longer works. Colosseum only shows this replacement once."
            : (root.purpose === "passwordRecovered"
            ? "Your old recovery key no longer works. Save the new one below."
            : (root.purpose === "deviceChallengeRecovered"
                ? "The key used for this sign-in is no longer valid. Colosseum only shows the replacement once."
                : "Colosseum only shows this key once."))
    }

    Item { width: 1; height: 24 }

    Rectangle {
        width: parent.width
        height: keyColumn.implicitHeight + 30
        radius: 14
        color: Qt.rgba(0.015, 0.018, 0.03, 0.70)
        border.width: 1
        border.color: keyTheme.edge

        Theme { id: keyTheme }

        Column {
            id: keyColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 15
            spacing: 10

            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: root.manualReplacement
                        ? "New recovery key"
                        : (root.purpose === "passwordRecovered"
                        || root.purpose === "deviceChallengeRecovered"
                        ? "New recovery key"
                        : "Your recovery key")
                    color: keyTheme.inkDim
                    font.family: keyTheme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    width: badgeText.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.12)
                    border.width: 1
                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.34)

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: "Shown once"
                        color: keyTheme.gold
                        font.family: keyTheme.ui
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                }
            }

            Text {
                objectName: "accountRecoveryKeyValue"
                width: parent.width
                text: root.presenter ? root.presenter.recoveryKey : ""
                color: keyTheme.ink
                font.family: "Cascadia Mono"
                font.pixelSize: 16
                font.letterSpacing: 0.8
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                Accessible.name: "Recovery key"
            }
        }
    }

    Item { width: 1; height: 16 }

    AccountButton {
        objectName: "accountRecoveryKeyCopy"
        width: parent.width
        visible: !root.manualReplacement
        text: "Copy recovery key"
        onClicked: root.copyPresentedKey()
    }

    Row {
        width: parent.width
        visible: root.manualReplacement
        spacing: 9

        AccountButton {
            objectName: "accountRecoveryKeyCopyManual"
            width: (parent.width - parent.spacing) / 2
            text: "Copy recovery key"
            onClicked: root.copyPresentedKey()
        }

        AccountButton {
            objectName: "accountRecoveryKeySavedManual"
            width: (parent.width - parent.spacing) / 2
            text: "I saved it"
            variant: "primary"
            onClicked: root.finishPresentedKey()
        }
    }

    Item {
        width: parent.width
        height: 0
        z: 20

        Rectangle {
            id: copyToast
            objectName: "accountRecoveryKeyCopyToast"
            width: Math.min(parent.width, copyToastText.implicitWidth + 28)
            height: 34
            radius: 17
            visible: false
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            color: Qt.rgba(0.08, 0.08, 0.11, 0.96)
            border.width: 1
            border.color: toastTheme.edge

            Theme { id: toastTheme }

            Text {
                id: copyToastText
                anchors.centerIn: parent
                text: "Recovery key copied."
                color: toastTheme.gold
                font.family: toastTheme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }

    Timer {
        id: copyToastTimer
        interval: 1500
        repeat: false
        onTriggered: copyToast.visible = false
    }

    Text {
        width: parent.width
        visible: root.presenter && root.presenter.copyState === "failed"
        text: "Couldn’t copy the recovery key safely. Save it manually."
        color: "#f0a3a3"
        font.family: failedTheme.ui
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        Theme { id: failedTheme }
    }

    Item { width: 1; height: 16 }

    AccountButton {
        objectName: "accountRecoveryKeyContinue"
        width: parent.width
        visible: !root.manualReplacement
        text: root.purpose === "passwordRecovered"
            ? "Continue to sign in"
            : "Continue to Colosseum"
        variant: "primary"
        onClicked: root.finishPresentedKey()
    }

    Item { width: 1; height: 18 }

    Text {
        width: parent.width
        text: root.manualReplacement
            ? "Store it somewhere outside this device. The key is not kept as ordinary Account Centre state."
            : (root.purpose === "passwordRecovered"
            || root.purpose === "deviceChallengeRecovered"
            ? "The old recovery key has been replaced."
            : "You can generate a new recovery key later while signed in.")
        color: footerTheme.inkDimmer
        font.family: footerTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Theme { id: footerTheme }
    }

    Text {
        width: parent.width
        visible: !root.manualReplacement
            && root.purpose !== "passwordRecovered"
            && root.purpose !== "deviceChallengeRecovered"
        text: "Generating a new one kills the old key."
        color: secondFooterTheme.inkDimmer
        font.family: secondFooterTheme.ui
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Theme { id: secondFooterTheme }
    }
}

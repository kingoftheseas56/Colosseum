import QtQuick
import ".."

Item {
    id: root
    objectName: "accountOnboardingHost"

    required property var controller
    required property var recoveryPresenter
    property Item backdrop: null
    property real topInset: 74

    function openSignIn() {
        controller.returnToSignIn()
        onboarding.goSignIn()
    }

    function openCreateAccount() {
        controller.returnToSignIn()
        onboarding.goCreate()
    }

    readonly property bool accountFlowVisible:
        controller.signOutSyncWarningPending
        || recoveryPresenter.active
        || controller.mode === "signedOut"
        || controller.mode === "authenticating"
        || controller.mode === "awaitingDeviceApproval"
        || controller.mode === "awaitingRecoveryApproval"
        || controller.mode === "locked"
        || controller.mode === "error"

    visible: accountFlowVisible

    AccountOnboarding {
        id: onboarding
        objectName: "accountOnboardingSurface"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.topInset
        controller: root.controller
        recoveryPresenter: root.recoveryPresenter
        backdrop: root.backdrop
        visible:
            !root.controller.signOutSyncWarningPending
        z: 1
    }

    AccountPendingSyncSignOut {
        id: pendingSyncSignOut
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.topInset
        controller: root.controller
        backdrop: root.backdrop
        visible:
            root.controller.signOutSyncWarningPending
        z: 2
    }
}

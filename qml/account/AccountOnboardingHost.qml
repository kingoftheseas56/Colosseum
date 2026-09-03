// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."
import "../SystemFocusContainment.js" as SystemFocusContainment

Item {
    id: root
    objectName: "accountOnboardingHost"

    required property var controller
    required property var recoveryPresenter
    property Item backdrop: null
    property real topInset: 74
    property Item focusReturnItem: null

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

    function focusFirstInside() {
        Qt.callLater(function() {
            if (root.visible)
                SystemFocusContainment.move(root.Window.window, root, true)
        })
    }

    function restoreInvoker() {
        const target = focusReturnItem
        focusReturnItem = null
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus()
        })
    }

    onVisibleChanged: {
        if (visible) {
            const active = root.Window.window ? root.Window.window.activeFocusItem : null
            if (active && !SystemFocusContainment.isWithin(active, root))
                focusReturnItem = active
            focusFirstInside()
        } else {
            restoreInvoker()
        }
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Tab) {
            const forward = !(event.modifiers & Qt.ShiftModifier)
            if (SystemFocusContainment.move(root.Window.window, root, forward))
                event.accepted = true
        }
    }

    Shortcut {
        sequence: "Tab"
        enabled: root.visible
        onActivated: SystemFocusContainment.move(root.Window.window, root, true)
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.visible
        onActivated: SystemFocusContainment.move(root.Window.window, root, false)
    }

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

    Connections {
        target: root.controller
        function onSignOutSyncWarningPendingChanged() {
            if (root.visible)
                root.focusFirstInside()
        }
        function onModeChanged() {
            if (root.visible)
                root.focusFirstInside()
        }
    }

    Connections {
        target: onboarding
        function onRouteNameChanged() {
            if (root.visible)
                root.focusFirstInside()
        }
    }
}

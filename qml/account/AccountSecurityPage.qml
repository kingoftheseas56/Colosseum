// AccountSecurityPage.qml
// Native QML conversion of the locked Account Centre Security mock.
// Approval, protection and session state remain controller-authoritative.
// Password strings live only in short-lived QML input fields.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountSecurityPage"

    property var controller: null
    property bool active: false

    property var approvalRequests: []
    property bool passwordExpanded: false
    property bool passwordRequestPending: false
    property bool logoutConfirmationOpen: false
    property bool logoutRequestPending: false
    property bool protectionRequestPending: false
    property string approvalDecisionKey: ""
    property string validationMessage: ""
    property bool approvalRefreshIssued: false

    readonly property bool signedIn: controller && controller.mode === "signedIn"
    readonly property bool compactRows: scroller.width < 620
    readonly property bool compactPassword: scroller.width < 760
    readonly property int approvalCount: modelLength(approvalRequests)
    readonly property int newPasswordCount: passwordCodePoints(newPasswordField.text)
    readonly property bool passwordMatches: newPasswordField.text === confirmPasswordField.text
    readonly property bool passwordFormValid: signedIn
        && currentPasswordField.text.length > 0
        && newPasswordCount >= 8
        && newPasswordCount <= 128
        && passwordMatches
        && !passwordRequestPending

    Theme { id: theme }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            if (root.logoutConfirmationOpen) {
                root.closeLogoutConfirmation()
                event.accepted = true
                return
            }
            if (root.passwordExpanded) {
                root.closePasswordEditor()
                event.accepted = true
                return
            }
        }
        keyboardScroll.handle(event)
    }

    function modelLength(model) {
        if (!model)
            return 0
        if (model.count !== undefined)
            return Number(model.count)
        if (model.length !== undefined)
            return Number(model.length)
        return 0
    }

    function modelAt(model, index) {
        if (!model || index < 0 || index >= modelLength(model))
            return null
        if (typeof model.get === "function")
            return model.get(index)
        return model[index]
    }

    function itemText(item, key) {
        if (!item || item[key] === undefined || item[key] === null)
            return ""
        return String(item[key])
    }

    function approvalChallengeId(item) {
        const snake = itemText(item, "challenge_id")
        if (snake.length > 0)
            return snake
        return itemText(item, "challengeId")
    }

    function approvalKind(item) {
        return itemText(item, "kind")
    }

    function approvalKey(item) {
        const kind = approvalKind(item)
        const challengeId = approvalChallengeId(item)
        return kind.length > 0 && challengeId.length > 0
            ? kind + "|" + challengeId
            : ""
    }

    function approvalTitle(item) {
        return qsTr("New sign-in waiting for approval")
    }

    function approvalDetail(item) {
        return qsTr("A Colosseum sign-in is waiting for your decision.")
    }

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

    function clearPasswordSecrets() {
        currentPasswordField.clear()
        newPasswordField.clear()
        confirmPasswordField.clear()
        validationMessage = ""
        passwordRequestPending = false
    }

    function openPasswordEditor() {
        validationMessage = ""
        passwordExpanded = true
        Qt.callLater(function() {
            if (root.active && root.passwordExpanded)
                currentPasswordField.forceInputFocus()
        })
    }

    function closePasswordEditor(restoreFocus) {
        clearPasswordSecrets()
        passwordExpanded = false
        if (restoreFocus !== false) {
            Qt.callLater(function() {
                if (root.active && passwordAction.visible && passwordAction.enabled)
                    passwordAction.forceActiveFocus()
            })
        }
    }

    function openLogoutConfirmation() {
        logoutConfirmationOpen = true
        Qt.callLater(function() {
            if (root.active && root.logoutConfirmationOpen)
                logoutCancel.forceActiveFocus()
        })
    }

    function closeLogoutConfirmation(restoreFocus) {
        logoutConfirmationOpen = false
        if (restoreFocus !== false) {
            Qt.callLater(function() {
                if (root.active && logoutAction.visible && logoutAction.enabled)
                    logoutAction.forceActiveFocus()
            })
        }
    }

    function clearEphemeralState() {
        closePasswordEditor(false)
        closeLogoutConfirmation(false)
        logoutRequestPending = false
        protectionRequestPending = false
        approvalDecisionKey = ""
        approvalRequests = []
    }

    function requestApprovals() {
        if (!approvalRefreshIssued && signedIn && controller) {
            approvalRefreshIssued = true
            controller.refreshApprovals()
        }
    }

    function requestProtectionToggle() {
        if (!signedIn || !controller || protectionRequestPending)
            return
        protectionRequestPending = true
        controller.setNewDeviceProtection(!controller.newDeviceProtection)
    }

    function requestApproval(item, approve) {
        if (!signedIn || !controller || approvalDecisionKey.length > 0)
            return

        const kind = approvalKind(item)
        const challengeId = approvalChallengeId(item)
        if (kind.length === 0 || challengeId.length === 0)
            return

        approvalDecisionKey = kind + "|" + challengeId
        controller.decideApproval(kind, challengeId, approve)
    }

    function submitPassword() {
        validationMessage = ""

        if (!passwordFormValid) {
            if (newPasswordCount < 8 || newPasswordCount > 128)
                validationMessage = qsTr("Use 8–128 characters.")
            else if (!passwordMatches)
                validationMessage = qsTr("Those passwords do not match.")
            return
        }

        passwordRequestPending = true
        controller.changePassword(
            currentPasswordField.text,
            newPasswordField.text)
    }

    function confirmLogoutEverywhere() {
        if (!signedIn || !controller || logoutRequestPending)
            return
        logoutRequestPending = true
        controller.logoutEverywhere()
    }

    onActiveChanged: {
        approvalRefreshIssued = false
        if (active)
            requestApprovals()
        else
            clearEphemeralState()
    }

    onControllerChanged: {
        if (active)
            requestApprovals()
    }

    Component.onCompleted: {
        if (active)
            requestApprovals()
    }

    Component.onDestruction: clearEphemeralState()

    Connections {
        target: root.controller
        enabled: root.controller !== null

        function onApprovalRequestsChanged(requests) {
            root.approvalRequests = requests || []
            root.approvalDecisionKey = ""
        }

        function onNewDeviceProtectionChanged() {
            root.protectionRequestPending = false
        }

        function onPasswordChangeSucceeded() {
            root.closePasswordEditor()
        }

        function onAccountError(category, code, message) {
            root.passwordRequestPending = false
            root.logoutRequestPending = false
            root.protectionRequestPending = false
            root.approvalDecisionKey = ""
        }

        function onSignedOut() {
            root.clearEphemeralState()
        }

        function onCurrentDeviceLocked() {
            root.clearEphemeralState()
        }

        function onSignOutSyncWarningPendingChanged() {
            if (root.controller && root.controller.signOutSyncWarningPending) {
                root.logoutRequestPending = false
                root.closeLogoutConfirmation(false)
            }
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: pageColumn.height + 58
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ScrollBar.vertical: ScrollBar {
            policy: scroller.contentHeight > scroller.height
                ? ScrollBar.AsNeeded
                : ScrollBar.AlwaysOff
        }

        Column {
            id: pageColumn
            width: Math.min(980, scroller.width)
            spacing: 0

            Text {
                text: qsTr("Security")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Item { width: 1; height: 34 }

            // Pending approvals displace the normal protection row.
            Column {
                id: approvalSection
                objectName: "securityApprovalSection"
                width: parent.width
                visible: root.approvalCount > 0
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                }

                Repeater {
                    model: root.approvalCount

                    Item {
                        id: approvalRow
                        property var entry: root.modelAt(root.approvalRequests, index)
                        property string requestKey: root.approvalKey(entry)
                        property bool requestValid: requestKey.length > 0
                        property bool decisionPending: root.approvalDecisionKey === requestKey
                        width: approvalSection.width
                        height: index === 0
                            ? firstApprovalContent.height + 58
                            : compactApprovalContent.height + 33
                        objectName: "securityApprovalRow" + index

                        Column {
                            id: firstApprovalContent
                            visible: index === 0
                            x: 0
                            y: 28
                            width: parent.width
                            spacing: 0

                            Text {
                                text: qsTr("New device").toUpperCase()
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 10
                                font.letterSpacing: 1.0
                            }

                            Item { width: 1; height: 10 }

                            Item {
                                width: parent.width
                                height: Math.max(firstApprovalTitle.implicitHeight, firstApprovalActions.height)

                                Text {
                                    id: firstApprovalTitle
                                    anchors.left: parent.left
                                    anchors.right: root.compactRows
                                        ? parent.right
                                        : firstApprovalActions.left
                                    anchors.rightMargin: root.compactRows ? 0 : 30
                                    text: root.approvalTitle(approvalRow.entry)
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 24
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Row {
                                    id: firstApprovalActions
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    visible: !root.compactRows
                                    spacing: 9

                                    AccountButton {
                                        objectName: "securityApprovalDeny" + index
                                        width: 88
                                        height: 34
                                        text: qsTr("Deny")
                                        enabled: root.signedIn
                                            && approvalRow.requestValid
                                            && root.approvalDecisionKey.length === 0
                                        onClicked: root.requestApproval(approvalRow.entry, false)
                                    }

                                    AccountButton {
                                        objectName: "securityApprovalApprove" + index
                                        width: 96
                                        height: 34
                                        text: approvalRow.decisionPending
                                            ? qsTr("Working…")
                                            : qsTr("Approve")
                                        variant: "primary"
                                        enabled: root.signedIn
                                            && approvalRow.requestValid
                                            && root.approvalDecisionKey.length === 0
                                        onClicked: root.requestApproval(approvalRow.entry, true)
                                    }
                                }
                            }

                            Row {
                                visible: root.compactRows
                                spacing: 9

                                AccountButton {
                                    objectName: "securityApprovalDenyCompact" + index
                                    width: 88
                                    height: 34
                                    text: qsTr("Deny")
                                    enabled: root.signedIn
                                        && approvalRow.requestValid
                                        && root.approvalDecisionKey.length === 0
                                    onClicked: root.requestApproval(approvalRow.entry, false)
                                }

                                AccountButton {
                                    objectName: "securityApprovalApproveCompact" + index
                                    width: 104
                                    height: 34
                                    text: approvalRow.decisionPending
                                        ? qsTr("Working…")
                                        : qsTr("Approve")
                                    variant: "primary"
                                    enabled: root.signedIn
                                        && approvalRow.requestValid
                                        && root.approvalDecisionKey.length === 0
                                    onClicked: root.requestApproval(approvalRow.entry, true)
                                }
                            }

                            Item { width: 1; height: 22 }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: theme.edge
                            }

                            Item {
                                width: parent.width
                                height: approvalDetailColumn.height + 32

                                Column {
                                    id: approvalDetailColumn
                                    anchors.left: parent.left
                                    anchors.right: pendingMeta.left
                                    anchors.rightMargin: 24
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4

                                    Text {
                                        width: parent.width
                                        text: qsTr("Colosseum sign-in")
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: root.approvalDetail(approvalRow.entry)
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        visible: !approvalRow.requestValid
                                        width: parent.width
                                        text: qsTr("This request is missing the identifiers required to approve or deny it.")
                                        color: "#f0a3a3"
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                Text {
                                    id: pendingMeta
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Pending")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: theme.edge
                            }
                        }

                        Item {
                            id: compactApprovalContent
                            visible: index > 0
                            x: 0
                            y: 16
                            width: parent.width
                            height: Math.max(compactApprovalCopy.height, compactApprovalActions.height)

                            Column {
                                id: compactApprovalCopy
                                anchors.left: parent.left
                                anchors.right: compactApprovalActions.left
                                anchors.rightMargin: root.compactRows ? 0 : 24
                                spacing: 3

                                Text {
                                    width: parent.width
                                    text: qsTr("Pending sign-in")
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: root.approvalDetail(approvalRow.entry)
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    visible: !approvalRow.requestValid
                                    width: parent.width
                                    text: qsTr("Missing request identifiers.")
                                    color: "#f0a3a3"
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                }
                            }

                            Row {
                                id: compactApprovalActions
                                anchors.right: parent.right
                                anchors.top: parent.top
                                spacing: 9

                                AccountButton {
                                    objectName: "securityApprovalDeny" + index
                                    width: 88
                                    height: 34
                                    text: qsTr("Deny")
                                    enabled: root.signedIn
                                        && approvalRow.requestValid
                                        && root.approvalDecisionKey.length === 0
                                    onClicked: root.requestApproval(approvalRow.entry, false)
                                }

                                AccountButton {
                                    objectName: "securityApprovalApprove" + index
                                    width: 96
                                    height: 34
                                    text: approvalRow.decisionPending
                                        ? qsTr("Working…")
                                        : qsTr("Approve")
                                    variant: "primary"
                                    enabled: root.signedIn
                                        && approvalRow.requestValid
                                        && root.approvalDecisionKey.length === 0
                                    onClicked: root.requestApproval(approvalRow.entry, true)
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                y: parent.height + 16
                                height: 1
                                color: theme.edge
                            }
                        }
                    }
                }
            }

            // Normal adaptive top state.
            Item {
                id: protectionRow
                objectName: "securityProtectionRow"
                width: parent.width
                height: protectionRowMain.height + 51
                visible: root.approvalCount === 0

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: theme.edge
                }

                Item {
                    id: protectionRowMain
                    x: 0
                    y: 25
                    width: parent.width
                    height: Math.max(protectionRowCopy.height, 38)

                    Row {
                        id: protectionRowCopy
                        anchors.left: parent.left
                        anchors.right: protectionSwitchWrap.left
                        anchors.rightMargin: 36
                        spacing: 15

                        Rectangle {
                            width: 38
                            height: 38
                            radius: 12
                            color: theme.glassTint
                            border.width: 1
                            border.color: theme.edge

                            AccountSecurityIcon {
                                anchors.centerIn: parent
                                kind: "shield"
                                glyphSize: 18
                                strokeColor: theme.gold
                            }
                        }

                        Column {
                            width: Math.max(0, protectionRowCopy.width - 53)
                            spacing: 5

                            Text {
                                width: parent.width
                                text: qsTr("New-device protection")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                text: qsTr("New sign-ins need approval from a trusted Colosseum device, or the recovery key.")
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.55
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                text: root.controller && root.controller.newDeviceProtection
                                    ? qsTr("Protection is on")
                                    : qsTr("Protection is off")
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 10
                            }
                        }
                    }

                    Item {
                        id: protectionSwitchWrap
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 46
                        height: 26

                        Button {
                            id: protectionSwitch
                            objectName: "securityProtectionSwitch"
                            anchors.fill: parent
                            enabled: root.signedIn && !root.protectionRequestPending
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: qsTr("Toggle new-device protection")
                            onClicked: root.requestProtectionToggle()

                            background: Rectangle {
                                radius: 13
                                color: root.controller && root.controller.newDeviceProtection
                                    ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.12)
                                    : "transparent"
                                border.width: protectionSwitch.activeFocus ? 2 : 1
                                border.color: root.controller && root.controller.newDeviceProtection
                                    ? theme.gold
                                    : (protectionSwitch.activeFocus ? theme.gold : theme.edge)

                                Rectangle {
                                    width: 18
                                    height: 18
                                    radius: 9
                                    y: 3
                                    x: root.controller && root.controller.newDeviceProtection ? 23 : 3
                                    color: root.controller && root.controller.newDeviceProtection
                                        ? theme.gold
                                        : theme.inkDimmer
                                    Behavior on x { NumberAnimation { duration: 120 } }
                                }
                            }

                            contentItem: Item {}
                        }
                    }
                }
            }

            // Password utility.
            Column {
                id: passwordBlock
                objectName: "securityPasswordRow"
                width: parent.width
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                }

                Item {
                    width: parent.width
                    height: passwordNormalRow.height + 50

                    Item {
                        id: passwordNormalRow
                        x: 0
                        y: 25
                        width: parent.width
                        height: Math.max(passwordRowMain.height, 38)

                        Row {
                            id: passwordRowMain
                            anchors.left: parent.left
                            anchors.right: passwordAction.left
                            anchors.rightMargin: root.compactRows ? 16 : 36
                            spacing: 15

                            Rectangle {
                                width: 38
                                height: 38
                                radius: 12
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge

                                AccountSecurityIcon {
                                    anchors.centerIn: parent
                                    kind: "lock"
                                    glyphSize: 18
                                    strokeColor: theme.gold
                                }
                            }

                            Column {
                                width: Math.max(0, passwordRowMain.width - 53)
                                spacing: 5

                                Text {
                                    width: parent.width
                                    text: qsTr("Password")
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: qsTr("Change the password used to sign in to this Colosseum account.")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.55
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        AccountButton {
                            id: passwordAction
                            objectName: "securityPasswordChange"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !root.passwordExpanded
                            width: 88
                            height: 34
                            text: qsTr("Change")
                            enabled: root.signedIn && !root.passwordRequestPending
                            onClicked: root.openPasswordEditor()
                        }
                    }
                }

                Column {
                    id: passwordEditor
                    objectName: "securityPasswordEditor"
                    width: parent.width
                    visible: root.passwordExpanded
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.edge
                    }

                    Item { width: 1; height: 22 }

                    Grid {
                        id: passwordGrid
                        objectName: "securityPasswordGrid"
                        width: parent.width
                        columns: root.compactPassword ? 1 : 3
                        columnSpacing: 14
                        rowSpacing: 14

                        AccountField {
                            id: currentPasswordField
                            width: root.compactPassword
                                ? passwordGrid.width
                                : (passwordGrid.width - passwordGrid.columnSpacing * 2) / 3
                            label: qsTr("Current password")
                            placeholderText: qsTr("Current password")
                            password: true
                            maximumLength: 512
                            controlObjectName: "securityCurrentPassword"
                            onAccepted: newPasswordField.forceInputFocus()
                        }

                        AccountField {
                            id: newPasswordField
                            width: currentPasswordField.width
                            label: qsTr("New password")
                            placeholderText: qsTr("New password")
                            password: true
                            maximumLength: 512
                            controlObjectName: "securityNewPassword"
                            onAccepted: confirmPasswordField.forceInputFocus()
                        }

                        AccountField {
                            id: confirmPasswordField
                            width: currentPasswordField.width
                            label: qsTr("Confirm new password")
                            placeholderText: qsTr("Confirm new password")
                            password: true
                            maximumLength: 512
                            controlObjectName: "securityConfirmPassword"
                            onAccepted: root.submitPassword()
                        }
                    }

                    Item { width: 1; height: 10 }

                    Text {
                        width: parent.width
                        text: {
                            if (root.validationMessage.length > 0)
                                return root.validationMessage
                            if (confirmPasswordField.text.length > 0 && !root.passwordMatches)
                                return qsTr("Those passwords do not match.")
                            return qsTr("Use 8–128 characters.")
                        }
                        color: root.validationMessage.length > 0
                            || (confirmPasswordField.text.length > 0 && !root.passwordMatches)
                            ? "#f0a3a3"
                            : theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    Item { width: 1; height: 14 }

                    Item {
                        width: parent.width
                        height: 34

                        Row {
                            anchors.right: parent.right
                            spacing: 9

                            AccountButton {
                                objectName: "securityPasswordCancel"
                                width: 88
                                height: 34
                                text: qsTr("Cancel")
                                enabled: !root.passwordRequestPending
                                onClicked: root.closePasswordEditor()
                            }

                            AccountButton {
                                objectName: "securityPasswordSave"
                                width: 158
                                height: 34
                                text: root.passwordRequestPending
                                    ? qsTr("Saving…")
                                    : qsTr("Save new password")
                                variant: "primary"
                                enabled: root.passwordFormValid
                                onClicked: root.submitPassword()
                            }
                        }
                    }

                    Item { width: 1; height: 25 }
                }
            }

            // Quiet emergency session action.
            Column {
                id: logoutBlock
                objectName: "securityLogoutEverywhereRow"
                width: parent.width
                spacing: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                }

                Item {
                    width: parent.width
                    height: logoutNormalRow.height + 50

                    Item {
                        id: logoutNormalRow
                        x: 0
                        y: 25
                        width: parent.width
                        height: Math.max(logoutRowMain.height, 38)

                        Row {
                            id: logoutRowMain
                            anchors.left: parent.left
                            anchors.right: logoutAction.left
                            anchors.rightMargin: root.compactRows ? 16 : 36
                            spacing: 15

                            Rectangle {
                                width: 38
                                height: 38
                                radius: 12
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge

                                AccountSecurityIcon {
                                    anchors.centerIn: parent
                                    kind: "logout"
                                    glyphSize: 18
                                    strokeColor: theme.gold
                                }
                            }

                            Column {
                                width: Math.max(0, logoutRowMain.width - 53)
                                spacing: 5

                                Text {
                                    width: parent.width
                                    text: qsTr("Sign out everywhere")
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: qsTr("End every Colosseum account session, including this one.")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.55
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        AccountButton {
                            id: logoutAction
                            objectName: "securityLogoutEverywhere"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 154
                            height: 34
                            text: qsTr("Sign out everywhere")
                            enabled: root.signedIn && !root.logoutRequestPending
                            onClicked: root.openLogoutConfirmation()
                        }
                    }
                }

                Column {
                    width: parent.width
                    visible: root.logoutConfirmationOpen
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.edge
                    }

                    Item { width: 1; height: 16 }

                    Item {
                        width: parent.width
                        height: Math.max(logoutConfirmCopy.implicitHeight, logoutConfirmActions.height)

                        Text {
                            id: logoutConfirmCopy
                            anchors.left: parent.left
                            anchors.right: logoutConfirmActions.left
                            anchors.rightMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Every signed-in device will need to sign in again.")
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                            lineHeightMode: Text.ProportionalHeight
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }

                        Row {
                            id: logoutConfirmActions
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 9

                            AccountButton {
                                id: logoutCancel
                                objectName: "securityLogoutCancel"
                                width: 88
                                height: 34
                                text: qsTr("Cancel")
                                enabled: !root.logoutRequestPending
                                onClicked: root.closeLogoutConfirmation()
                            }

                            AccountButton {
                                objectName: "securityLogoutConfirm"
                                width: 136
                                height: 34
                                text: root.logoutRequestPending
                                    ? qsTr("Signing out…")
                                    : qsTr("Confirm sign out")
                                variant: "primary"
                                enabled: root.signedIn && !root.logoutRequestPending
                                onClicked: root.confirmLogoutEverywhere()
                            }
                        }
                    }

                    Item { width: 1; height: 25 }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                }
            }

            Item { width: 1; height: 12 }

            Text {
                objectName: "securityInlineError"
                width: parent.width
                visible: root.controller
                    && root.controller.lastErrorMessage
                    && root.controller.lastErrorMessage.length > 0
                text: root.controller ? root.controller.lastErrorMessage : ""
                color: "#f0a3a3"
                font.family: theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}

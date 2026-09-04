// AccountRecoveryPage.qml
// Native QML conversion of the locked Account Centre Recovery mock.
// The page can request recovery-key replacement, but never owns or renders the secret.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountRecoveryPage"

    property var controller: null
    property var presenter: null
    property bool active: false

    property bool replaceExpanded: false
    property bool requestPending: false
    property bool nativeErrorVisible: false
    property string nativeErrorMessage: ""
    property string validationMessage: ""

    readonly property bool signedIn: controller && controller.mode === "signedIn"
    readonly property bool compact: scroller.width < 820

    Theme { id: theme }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && root.replaceExpanded) {
            root.cancelReplacement()
            event.accepted = true
            return
        }
        keyboardScroll.handle(event)
    }

    function clearPassword() {
        currentPasswordField.clear()
    }

    function clearReplacementState() {
        clearPassword()
        replaceExpanded = false
        requestPending = false
        nativeErrorVisible = false
        nativeErrorMessage = ""
        validationMessage = ""
    }

    function beginReplacement() {
        if (!signedIn || requestPending)
            return
        validationMessage = ""
        nativeErrorVisible = false
        nativeErrorMessage = ""
        replaceExpanded = true
        Qt.callLater(function() {
            if (root.active && root.replaceExpanded)
                currentPasswordField.forceInputFocus()
        })
    }

    function cancelReplacement() {
        clearReplacementState()
        if (active) {
            Qt.callLater(function() {
                if (root.active && replaceButton.visible)
                    replaceButton.forceActiveFocus()
            })
        }
    }

    function submitReplacement() {
        if (!signedIn || !controller || requestPending)
            return

        validationMessage = ""
        nativeErrorVisible = false
        nativeErrorMessage = ""

        if (currentPasswordField.text.length === 0) {
            validationMessage = qsTr("Enter your current password.")
            currentPasswordField.forceInputFocus()
            return
        }

        requestPending = true
        controller.replaceRecoveryKey(currentPasswordField.text)
    }

    function reconcilePresenter() {
        if (!presenter
            || !presenter.active
            || presenter.purpose !== "manualReplacement") {
            return
        }

        clearPassword()
        replaceExpanded = false
        requestPending = false
        nativeErrorVisible = false
        nativeErrorMessage = ""
        validationMessage = ""
    }

    onActiveChanged: {
        if (!active)
            clearReplacementState()
        else
            reconcilePresenter()
    }

    onPresenterChanged: reconcilePresenter()

    Component.onCompleted: reconcilePresenter()
    Component.onDestruction: clearReplacementState()

    Connections {
        target: root.controller
        enabled: root.controller !== null
        ignoreUnknownSignals: true

        function onRecoveryKeyReplacementSucceeded() {
            if (!root.requestPending)
                return

            // Service success never substitutes for one-time presenter truth.
            // Stop duplicate submissions, then let presenter activation own the
            // actual completion/collapse when the key has been delivered.
            root.requestPending = false
            root.reconcilePresenter()
        }

        function onRecoveryKeyReplacementFailed(message) {
            if (!root.requestPending)
                return

            root.requestPending = false
            root.clearPassword()
            root.nativeErrorVisible = true
            root.nativeErrorMessage = message
                || qsTr("The recovery key could not be replaced.")
            Qt.callLater(function() {
                if (root.active && root.replaceExpanded)
                    currentPasswordField.forceInputFocus()
            })
        }

        function onSignedOut() {
            root.clearReplacementState()
        }

        function onCurrentDeviceLocked() {
            root.clearReplacementState()
        }
    }

    Connections {
        target: root.presenter
        enabled: root.presenter !== null

        function onActiveChanged() {
            root.reconcilePresenter()
        }

        function onPurposeChanged() {
            root.reconcilePresenter()
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
                text: qsTr("Recovery")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Item { width: 1; height: 34 }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }

            Item { width: 1; height: 30 }

            Grid {
                id: heroGrid
                objectName: "recoveryHeroGrid"
                width: parent.width
                columns: root.compact ? 1 : 2
                columnSpacing: root.compact ? 0 : 34
                rowSpacing: root.compact ? 18 : 0

                Item {
                    width: root.compact ? heroGrid.width : 148
                    height: keyMark.height

                    Rectangle {
                        id: keyMark
                        width: root.compact ? 92 : 126
                        height: width
                        radius: width / 2
                        color: Qt.rgba(1, 1, 1, 0.035)
                        border.width: 1
                        border.color: theme.edge

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 8
                            radius: width / 2
                            color: theme.glassTint
                            opacity: 0.42
                        }

                        AccountRecoveryIcon {
                            anchors.centerIn: parent
                            kind: "key"
                            glyphSize: root.compact ? 38 : 48
                            strokeWidth: 1.4
                            strokeColor: theme.gold
                        }
                    }
                }

                Item {
                    width: root.compact
                        ? heroGrid.width
                        : Math.max(0, heroGrid.width - 182)
                    height: heroCopyColumn.implicitHeight

                    Column {
                        id: heroCopyColumn
                        width: parent.width
                        spacing: 0

                        Text {
                            width: parent.width
                            text: qsTr("Your way back in.")
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 27
                            font.weight: Font.DemiBold
                            font.letterSpacing: -0.9
                            wrapMode: Text.WordWrap
                        }

                        Item { width: 1; height: 12 }

                        Text {
                            width: Math.min(parent.width, 610)
                            text: qsTr("Your recovery key can reset your password if you lose access. Colosseum showed the current key once when it was created and does not keep it available for reveal here.")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 12
                            lineHeightMode: Text.ProportionalHeight
                            lineHeight: 1.65
                            wrapMode: Text.WordWrap
                        }

                        Item { width: 1; height: 18 }

                        Row {
                            spacing: 9

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: theme.gold
                            }

                            Text {
                                text: qsTr("Only the newest recovery key works.")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 32 }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }

            Column {
                id: replaceSection
                objectName: "recoveryReplaceSection"
                width: parent.width
                spacing: 0

                Item { width: 1; height: 27 }

                Item {
                    id: replaceHeader
                    width: parent.width
                    height: root.compact
                        ? replaceMain.height + (replaceButton.visible ? 48 : 0)
                        : Math.max(replaceMain.height, replaceButton.visible ? replaceButton.height : 0)

                    Row {
                        id: replaceMain
                        width: root.compact
                            ? parent.width
                            : Math.max(0, parent.width - (replaceButton.visible ? replaceButton.width + 36 : 0))
                        spacing: 15

                        Rectangle {
                            width: 38
                            height: 38
                            radius: 12
                            color: theme.glassTint
                            border.width: 1
                            border.color: theme.edge

                            AccountRecoveryIcon {
                                anchors.centerIn: parent
                                kind: "replace"
                                glyphSize: 18
                                strokeColor: theme.inkDim
                            }
                        }

                        Column {
                            width: Math.max(0, replaceMain.width - 53)
                            spacing: 5

                            Text {
                                width: parent.width
                                text: qsTr("Replace recovery key")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: Math.min(parent.width, 620)
                                text: qsTr("Generate a new recovery key if the old one is lost, exposed, or no longer somewhere you trust.")
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
                        id: replaceButton
                        objectName: "recoveryReplaceButton"
                        width: 112
                        height: 34
                        x: root.compact ? 53 : parent.width - width
                        y: root.compact ? replaceMain.height + 14 : 2
                        text: qsTr("Replace key")
                        visible: !root.replaceExpanded
                        enabled: root.signedIn && !root.requestPending
                        Accessible.name: qsTr("Replace recovery key")
                        onClicked: root.beginReplacement()
                    }
                }

                Column {
                    id: replacementForm
                    objectName: "recoveryReplacementForm"
                    width: parent.width
                    visible: root.replaceExpanded
                    spacing: 0

                    Item { width: 1; height: 22 }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.09)
                    }

                    Item { width: 1; height: 22 }

                    Row {
                        width: parent.width
                        spacing: 12

                        Rectangle {
                            width: 25
                            height: 25
                            radius: 8
                            color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.35)

                            Text {
                                anchors.centerIn: parent
                                text: "!"
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }

                        Column {
                            width: Math.max(0, parent.width - 37)
                            spacing: 4

                            Text {
                                width: parent.width
                                text: qsTr("The old key will stop working.")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                text: qsTr("After replacement, save the new key before leaving the one-time screen.")
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 10
                                lineHeightMode: Text.ProportionalHeight
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Item { width: 1; height: 18 }

                    AccountField {
                        id: currentPasswordField
                        width: Math.min(460, parent.width)
                        label: qsTr("Current password")
                        placeholderText: qsTr("Enter your current password")
                        password: true
                        maximumLength: 512
                        controlObjectName: "recoveryCurrentPassword"
                        onAccepted: root.submitReplacement()
                    }

                    Text {
                        width: Math.min(620, parent.width)
                        visible: root.validationMessage.length > 0
                            || (root.nativeErrorVisible
                                && root.nativeErrorMessage.length > 0)
                        text: root.validationMessage.length > 0
                            ? root.validationMessage
                            : root.nativeErrorMessage
                        color: "#f0a3a3"
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Item { width: 1; height: 14 }

                    Row {
                        spacing: 9

                        AccountButton {
                            objectName: "recoveryReplaceCancel"
                            width: 92
                            height: 34
                            text: qsTr("Cancel")
                            enabled: !root.requestPending
                            onClicked: root.cancelReplacement()
                        }

                        AccountButton {
                            objectName: "recoveryGenerateButton"
                            width: 160
                            height: 34
                            text: root.requestPending
                                ? qsTr("Generating…")
                                : qsTr("Generate new key")
                            variant: "primary"
                            enabled: root.signedIn && !root.requestPending
                            onClicked: root.submitReplacement()
                        }
                    }
                }

                Item { width: 1; height: 27 }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: theme.edge
                }
            }

            Item {
                id: passwordLossSection
                objectName: "recoveryPasswordLossSection"
                width: parent.width
                height: passwordLossMain.height + 54

                Row {
                    id: passwordLossMain
                    x: 0
                    y: 27
                    width: parent.width
                    spacing: 15

                    Rectangle {
                        width: 38
                        height: 38
                        radius: 12
                        color: theme.glassTint
                        border.width: 1
                        border.color: theme.edge

                        AccountRecoveryIcon {
                            anchors.centerIn: parent
                            kind: "recovery"
                            glyphSize: 18
                            strokeColor: theme.inkDim
                        }
                    }

                    Column {
                        width: Math.max(0, passwordLossMain.width - 53)
                        spacing: 5

                        Text {
                            width: parent.width
                            text: qsTr("If you lose your password")
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: Math.min(parent.width, 620)
                            text: qsTr("Sign out and choose account recovery. Your username, recovery key, and a new password are enough to recover the account.")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            lineHeightMode: Text.ProportionalHeight
                            lineHeight: 1.55
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: theme.edge
            }
        }
    }
}

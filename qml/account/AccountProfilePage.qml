// AccountProfilePage.qml
// Native QML conversion of the locked Account Centre Profile Pass 2 mock.
// The page reads identity from AccountController and never treats draft UI state as persisted truth.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root
    objectName: "accountProfilePage"

    property var inputMethod: Qt.inputMethod
    property var controller: null
    property bool active: false
    property string usernameDraft: ""
    property string usernameBaseline: ""
    property string pendingUsername: ""
    property bool profileUpdated: false
    property bool usernameRequestPending: false
    property bool avatarRequestPending: false
    property string operationErrorMessage: ""

    readonly property string currentUsername: controller ? controller.username : ""
    readonly property string persistedAvatarId: normalizedAvatarId(
        controller && controller.avatarId !== undefined ? controller.avatarId : "")
    readonly property bool usernameDirty: usernameDraft !== usernameBaseline
    readonly property bool saveEnabled: controller
        && controller.mode === "signedIn"
        && !usernameRequestPending
        && usernameDraft.trim().length > 0
        && usernameDraft.trim() !== currentUsername.trim()

    Theme { id: theme }

    AccountKeyboardViewport {
        id: keyboardViewport
        container: root
        flick: scroller
        inputMethod: root.inputMethod
    }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        keyboardScroll.handle(event)
    }
    Keys.onReleased: function(event) {
        keyboardScroll.handleRelease(event)
    }

    function normalizedAvatarId(value) {
        switch (value) {
        case "initial":
        case "laurel":
        case "column":
        case "book":
        case "screen":
        case "panels":
            return value
        default:
            return "initial"
        }
    }

    function usernameInitial() {
        const value = currentUsername.trim()
        return value.length > 0 ? value.charAt(0).toUpperCase() : "?"
    }

    function avatarLabel(value) {
        switch (value) {
        case "laurel": return qsTr("Laurel")
        case "column": return qsTr("Column")
        case "book": return qsTr("Book")
        case "screen": return qsTr("Screen")
        case "panels": return qsTr("Panels")
        default: return qsTr("Initial")
        }
    }

    function focusAvatarAt(index, step) {
        let candidate = index
        while (candidate >= 0 && candidate < avatarRepeater.count) {
            const item = avatarRepeater.itemAt(candidate)
            if (item && item.visible && item.enabled) {
                item.forceActiveFocus()
                return true
            }
            candidate += step
        }
        return false
    }

    function handleAvatarKey(event, index) {
        let target = index
        let step = 0
        if (event.key === Qt.Key_Left) {
            target -= 1
            step = -1
        } else if (event.key === Qt.Key_Right) {
            target += 1
            step = 1
        } else if (event.key === Qt.Key_Up) {
            target -= avatarGrid.columns
            step = -avatarGrid.columns
        } else if (event.key === Qt.Key_Down) {
            target += avatarGrid.columns
            step = avatarGrid.columns
        } else if (event.key === Qt.Key_Home) {
            target = 0
            step = 1
        } else if (event.key === Qt.Key_End) {
            target = avatarRepeater.count - 1
            step = -1
        } else {
            return false
        }

        if (focusAvatarAt(target, step)) {
            event.accepted = true
            return true
        }
        event.accepted = false
        return false
    }

    function syncDraftFromController() {
        usernameBaseline = currentUsername
        usernameDraft = currentUsername
    }

    function reconcileUsernameFromController() {
        if (usernameDraft.trim() === currentUsername.trim()) {
            syncDraftFromController()
            return
        }

        // A background refresh may update controller.username while the user is
        // typing. Only adopt that value automatically when there is no local edit.
        if (!usernameDirty && !usernameRequestPending)
            syncDraftFromController()
    }

    function saveUsername() {
        if (!saveEnabled)
            return

        const requested = usernameDraft.trim()
        profileUpdated = false
        operationErrorMessage = ""
        usernameRequestPending = true
        pendingUsername = requested
        controller.renameUsername(requested)
    }

    function requestAvatar(avatarId) {
        if (!controller
            || controller.mode !== "signedIn"
            || avatarRequestPending
            || persistedAvatarId === avatarId) {
            return
        }

        profileUpdated = false
        operationErrorMessage = ""
        avatarRequestPending = true
        controller.setBuiltinAvatar(avatarId)
    }

    function clearEphemeralState() {
        usernameRequestPending = false
        avatarRequestPending = false
        pendingUsername = ""
        profileUpdated = false
        operationErrorMessage = ""
    }

    onControllerChanged: syncDraftFromController()

    // Keyed to visibility (which the composed host now drives from `active`, see
    // AccountCenter.qml): losing visibility clears ephemeral request/toast state so a
    // later close+reopen never resurfaces a stale pending save or error, and gaining
    // visibility resyncs the draft from the controller baseline.
    onVisibleChanged: {
        if (!visible) {
            clearEphemeralState()
            return
        }
        // syncDraftFromController() is null-controller-safe (currentUsername falls back
        // to ""), so always resync the draft on becoming visible even before a
        // controller is attached; only the live refresh call needs a real controller.
        syncDraftFromController()
        operationErrorMessage = ""
        if (controller && controller.mode === "signedIn")
            controller.refreshProfile()
    }

    Component.onCompleted: {
        syncDraftFromController()
        if (visible && controller && controller.mode === "signedIn")
            controller.refreshProfile()
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true

        function onUsernameChanged() {
            root.reconcileUsernameFromController()
        }

        function onUsernameRenameSucceeded() {
            root.usernameRequestPending = false
            if (root.currentUsername.trim() === root.pendingUsername)
                root.syncDraftFromController()
            root.pendingUsername = ""
            root.operationErrorMessage = ""
            root.profileUpdated = true
            updatedTimer.restart()
        }

        function onUsernameRenameFailed(message) {
            root.usernameRequestPending = false
            root.pendingUsername = ""
            root.profileUpdated = false
            root.operationErrorMessage = message || qsTr("Could not update the username.")
        }

        function onBuiltinAvatarChangeSucceeded() {
            root.avatarRequestPending = false
            root.operationErrorMessage = ""
            root.profileUpdated = true
            updatedTimer.restart()
        }

        function onBuiltinAvatarChangeFailed(message) {
            root.avatarRequestPending = false
            root.profileUpdated = false
            root.operationErrorMessage = message || qsTr("Could not update the avatar.")
        }

        function onSignedOut() {
            root.clearEphemeralState()
            root.syncDraftFromController()
        }

        function onCurrentDeviceLocked() {
            root.clearEphemeralState()
            root.syncDraftFromController()
        }
    }

    Timer {
        id: updatedTimer
        interval: 2200
        repeat: false
        onTriggered: root.profileUpdated = false
    }

    Flickable {
        id: scroller
        objectName: "accountProfileScrollRegion"
        anchors.fill: parent
        anchors.bottomMargin: keyboardViewport.keyboardOverlap
        clip: true
        contentWidth: width
        contentHeight: Math.max(pageColumn.implicitHeight + 24, keyboardViewport.focusedScrollExtent)
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: HouseScrollBar { flick: scroller; glassStyle: true }

        Column {
            id: pageColumn
            width: Math.min(680, Math.max(0, scroller.width - 16))
            spacing: 22

            Text {
                width: parent.width
                text: qsTr("Profile")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 31
            }

            Rectangle {
                width: parent.width
                height: identity.implicitHeight + 32
                radius: 16
                color: theme.glassTint
                border.color: theme.edge

                RowLayout {
                    id: identity
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Rectangle {
                        Layout.preferredWidth: pageColumn.width < 380 ? 54 : 76
                        Layout.preferredHeight: Layout.preferredWidth
                        Layout.alignment: Qt.AlignVCenter
                        radius: width / 2
                        color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.08)
                        border.color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.42)

                        AccountAvatarGlyph {
                            anchors.centerIn: parent
                            width: parent.width * 0.55
                            height: width
                            avatarId: root.persistedAvatarId
                            initial: root.usernameInitial()
                            strokeColor: theme.gold
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: root.currentUsername || qsTr("Colosseum account")
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: pageColumn.width < 380 ? 18 : 23
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 medallion").arg(root.avatarLabel(root.persistedAvatarId))
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 10

                Text {
                    text: qsTr("Profile medallion")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Grid {
                    id: avatarGrid
                    width: parent.width
                    readonly property int cellWidth: 66
                    columns: Math.max(1, Math.min(avatarRepeater.count,
                        Math.floor((width + columnSpacing) / (cellWidth + columnSpacing))))
                    columnSpacing: 10
                    rowSpacing: 8

                    Repeater {
                        id: avatarRepeater
                        model: [
                            { id: "initial", label: qsTr("Initial") },
                            { id: "laurel", label: qsTr("Laurel") },
                            { id: "column", label: qsTr("Column") },
                            { id: "book", label: qsTr("Book") },
                            { id: "screen", label: qsTr("Screen") },
                            { id: "panels", label: qsTr("Panels") }
                        ]

                        Button {
                            id: avatarButton
                            required property var modelData
                            required property int index
                            readonly property bool selected: root.persistedAvatarId === modelData.id
                            width: avatarGrid.cellWidth
                            height: avatarContents.implicitHeight + 4
                            padding: 2
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            enabled: root.controller && root.controller.mode === "signedIn"
                                && !root.avatarRequestPending
                            Accessible.name: qsTr("Use %1 medallion").arg(modelData.label)
                            Accessible.description: selected ? qsTr("Current medallion") : ""
                            Keys.onPressed: function(event) { root.handleAvatarKey(event, index) }
                            onClicked: root.requestAvatar(modelData.id)

                            background: Item {}

                            contentItem: Column {
                                id: avatarContents
                                spacing: 6

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 50
                                    height: 50
                                    radius: 25
                                    color: avatarButton.selected
                                        ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.12)
                                        : avatarButton.hovered ? theme.glassHi : theme.glassTint
                                    border.width: avatarButton.activeFocus ? 2 : 1
                                    border.color: avatarButton.selected || avatarButton.activeFocus
                                        ? theme.gold : theme.edge

                                    AccountAvatarGlyph {
                                        anchors.centerIn: parent
                                        width: 25
                                        height: 25
                                        avatarId: avatarButton.modelData.id
                                        initial: root.usernameInitial()
                                        strokeColor: avatarButton.selected ? theme.gold : theme.inkDim
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: avatarButton.modelData.label
                                    color: avatarButton.selected ? theme.ink : theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: theme.edge }

            GridLayout {
                id: usernameForm
                width: Math.min(parent.width, 560)
                columns: width < 360 ? 1 : 2
                rowSpacing: 10
                columnSpacing: 12

                AccountField {
                    id: usernameField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    label: qsTr("Username")
                    text: root.usernameDraft
                    enabled: root.controller && root.controller.mode === "signedIn" && !root.usernameRequestPending
                    controlObjectName: "accountProfileUsername"
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
                    onTextChanged: {
                        if (root.usernameDraft !== text) {
                            root.usernameDraft = text
                            root.profileUpdated = false
                            root.operationErrorMessage = ""
                        }
                    }
                    onAccepted: root.saveUsername()
                }

                AccountButton {
                    Layout.alignment: usernameForm.columns === 1 ? Qt.AlignLeft : Qt.AlignBottom
                    Layout.preferredWidth: Math.min(implicitWidth, usernameForm.width)
                    text: root.usernameRequestPending ? qsTr("Saving…") : qsTr("Save changes")
                    variant: root.saveEnabled ? "primary" : "secondary"
                    enabled: root.saveEnabled
                    onClicked: root.saveUsername()
                }
            }

            Text {
                width: parent.width
                visible: root.profileUpdated || root.operationErrorMessage.length > 0
                text: root.profileUpdated ? qsTr("Profile updated") : root.operationErrorMessage
                color: root.profileUpdated ? theme.inkDim : "#e9857d"
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}

// AccountProfilePage.qml
// Native QML conversion of the locked Account Centre Profile Pass 2 mock.
// The page reads identity from AccountController and never treats draft UI state as persisted truth.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountProfilePage"

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

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        keyboardScroll.handle(event)
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
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: pageColumn.height + 48
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: scroller.contentHeight > scroller.height
                ? ScrollBar.AsNeeded
                : ScrollBar.AlwaysOff
        }

        Column {
            id: pageColumn
            width: Math.min(1010, scroller.width)
            spacing: 0

            Text {
                text: qsTr("Profile")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Item {
                id: profileStage
                width: parent.width
                height: wideStage ? 190 : 310
                property bool wideStage: width >= 560

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 30
                    height: 1
                    color: theme.edge
                }

                Rectangle {
                    id: heroMedallion
                    x: 0
                    y: 62
                    width: 126
                    height: 126
                    radius: 63
                    color: theme.glassTint
                    border.width: 1
                    border.color: theme.edge

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 8
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(
                            theme.gold.r,
                            theme.gold.g,
                            theme.gold.b,
                            0.40)
                        opacity: 0.55
                    }

                    AccountAvatarGlyph {
                        anchors.centerIn: parent
                        width: root.persistedAvatarId === "initial" ? 64 : 58
                        height: width
                        avatarId: root.persistedAvatarId
                        initial: root.usernameInitial()
                        strokeColor: theme.gold
                    }
                }

                Column {
                    id: heroCopy
                    x: profileStage.wideStage ? 174 : 0
                    y: profileStage.wideStage ? 70 : 208
                    width: profileStage.wideStage
                        ? profileStage.width - x
                        : profileStage.width
                    spacing: 0

                    Text {
                        width: parent.width
                        text: root.currentUsername
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 31
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Item { width: 1; height: 7 }

                    Text {
                        text: qsTr("Colosseum account")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }

                    Item { width: 1; height: 20 }

                    Rectangle {
                        width: 40
                        height: 1
                        color: theme.edge
                    }

                    Item { width: 1; height: 15 }

                    Row {
                        spacing: 8

                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            color: "transparent"
                            border.width: 1
                            border.color: theme.gold

                            Text {
                                anchors.centerIn: parent
                                text: "✓"
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }

                        Text {
                            text: qsTr("%1 medallion")
                                .arg(root.avatarLabel(root.persistedAvatarId))
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.edge
                }
            }

            Item {
                id: avatarSection
                width: parent.width
                height: avatarGrid.y + avatarGrid.height + 30

                Item {
                    id: avatarHeader
                    x: 0
                    y: 28
                    width: parent.width
                    height: 22

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Avatar")
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        id: statusPill
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: statusText.implicitWidth + 14
                        height: 22
                        radius: 11
                        color: "transparent"
                        border.width: 1
                        border.color: theme.edge

                        Text {
                            id: statusText
                            anchors.centerIn: parent
                            text: qsTr("BUILT-IN")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                            font.letterSpacing: 0.8
                        }
                    }
                }

                Grid {
                    id: avatarGrid
                    x: 0
                    y: 70
                    width: parent.width
                    columns: Math.max(2, Math.min(7, Math.floor(width / 88)))
                    columnSpacing: 14
                    rowSpacing: 14
                    height: childrenRect.height

                    Repeater {
                        id: avatarRepeater
                        model: [
                            { id: "initial", label: qsTr("Initial"), deferred: false },
                            { id: "laurel", label: qsTr("Laurel"), deferred: false },
                            { id: "column", label: qsTr("Column"), deferred: false },
                            { id: "book", label: qsTr("Book"), deferred: false },
                            { id: "screen", label: qsTr("Screen"), deferred: false },
                            { id: "panels", label: qsTr("Panels"), deferred: false },
                            { id: "custom", label: qsTr("Custom"), deferred: true }
                        ]

                        Button {
                            id: avatarButton
                            width: 74
                            height: 84
                            padding: 0
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            enabled: !modelData.deferred
                                && root.controller
                                && root.controller.mode === "signedIn"
                                && !root.avatarRequestPending
                            Accessible.name: modelData.deferred
                                ? qsTr("Custom avatar coming later")
                                : qsTr("Use %1 avatar").arg(modelData.label)
                            Keys.onPressed: function(event) {
                                root.handleAvatarKey(event, index)
                            }

                            readonly property bool selected:
                                !modelData.deferred
                                && root.persistedAvatarId === modelData.id

                            background: Item {}

                            contentItem: Item {
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 2
                                    width: 72
                                    height: 72
                                    radius: 36
                                    visible: avatarButton.selected || avatarButton.activeFocus
                                    color: "transparent"
                                    border.width: avatarButton.activeFocus ? 2 : 1
                                    border.color: theme.gold
                                    opacity: avatarButton.activeFocus ? 1.0 : 0.45
                                }

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 7
                                    width: 62
                                    height: 62
                                    radius: 31
                                    color: avatarButton.selected
                                        ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.10)
                                        : (avatarButton.hovered ? theme.glassHi : theme.glassTint)
                                    border.width: 1
                                    border.color: avatarButton.selected
                                        ? theme.gold
                                        : theme.edge
                                    opacity: modelData.deferred ? 0.48 : 1.0

                                    AccountAvatarGlyph {
                                        anchors.centerIn: parent
                                        width: modelData.id === "initial" ? 32 : 28
                                        height: width
                                        avatarId: modelData.id
                                        initial: root.usernameInitial()
                                        strokeColor: avatarButton.selected
                                            ? theme.gold
                                            : theme.inkDim
                                    }
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 76
                                    text: modelData.deferred
                                        ? qsTr("Custom  Later")
                                        : modelData.label
                                    color: theme.inkDimmer
                                    opacity: modelData.deferred ? 0.48 : 1.0
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                }
                            }

                            onClicked: root.requestAvatar(modelData.id)
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.edge
                }
            }

            Item {
                id: usernameSection
                width: parent.width
                height: usernameStatus.y + Math.max(18, usernameStatus.implicitHeight) + 30

                Text {
                    x: 0
                    y: 28
                    text: qsTr("Username")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Item {
                    id: usernameBody
                    x: 0
                    y: 66
                    width: Math.min(620, parent.width)
                    height: usernameLayoutNarrow ? 128 : 72
                    property bool usernameLayoutNarrow: width < 430

                    AccountField {
                        id: usernameField
                        width: usernameBody.usernameLayoutNarrow
                            ? parent.width
                            : parent.width - saveButton.width - 12
                        label: qsTr("Username")
                        text: root.usernameDraft
                        enabled: !root.usernameRequestPending
                        controlObjectName: "accountProfileUsername"
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
                        id: saveButton
                        anchors.left: usernameBody.usernameLayoutNarrow
                            ? parent.left
                            : usernameField.right
                        anchors.leftMargin: usernameBody.usernameLayoutNarrow ? 0 : 12
                        anchors.top: usernameBody.usernameLayoutNarrow
                            ? usernameField.bottom
                            : usernameField.top
                        anchors.topMargin: usernameBody.usernameLayoutNarrow ? 10 : 18
                        width: 122
                        height: 42
                        text: root.usernameRequestPending
                            ? qsTr("Saving…")
                            : qsTr("Save changes")
                        variant: "primary"
                        enabled: root.saveEnabled
                        onClicked: root.saveUsername()
                    }
                }

                Text {
                    id: usernameStatus
                    x: 0
                    y: usernameBody.y + usernameBody.height + 6
                    width: parent.width
                    visible: root.profileUpdated
                        || root.operationErrorMessage.length > 0
                    text: root.profileUpdated
                        ? qsTr("Profile updated")
                        : root.operationErrorMessage
                    color: root.profileUpdated ? theme.inkDimmer : "#e9857d"
                    font.family: theme.ui
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.edge
                }
            }
        }
    }
}

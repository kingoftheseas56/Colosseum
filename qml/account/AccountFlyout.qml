// Anchored top-bar menu. The full-screen click catcher keeps outside taps
// from reaching the shell while the menu is open.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../SystemFocusContainment.js" as SystemFocusContainment

Item {
    id: root

    property var controller: null
    property var accountSettings: null
    property string initial: "?"
    property Item focusReturnItem: null
    property bool stremioAvailable: typeof stremioSyncState !== "undefined" && !!stremioSyncState
    property bool nuvioAvailable: typeof NuvioAccounts !== "undefined" && !!NuvioAccounts
        && NuvioAccounts.available
    signal theatreSyncRequested(Item invoker)

    readonly property bool accountPresent: controller
        && (controller.mode === "signedIn" || controller.mode === "offline")
    readonly property bool localDevice: controller && controller.mode === "localOnly"
    readonly property var menuEntries: {
        const entries = []
        if (localDevice && controller.accountFeaturesAvailable !== false)
            entries.push({ section: "signIn", label: qsTr("Sign in to Colosseum") })
        if (accountPresent) {
            entries.push({ section: "profile", label: qsTr("Account settings") })
            entries.push({ section: "devices", label: qsTr("Devices") })
        }
        if ((localDevice || accountPresent) && (stremioAvailable || nuvioAvailable))
            entries.push({ section: "theatre", label: qsTr("Theatre sync") })
        if (localDevice || accountPresent)
            entries.push({ section: "privacy", label: qsTr("Data & privacy") })
        if (accountPresent && controller.syncRetryAvailable)
            entries.push({ section: "retry", label: qsTr("Retry cloud sync") })
        if (accountPresent)
            entries.push({ section: "signOut", label: qsTr("Sign out") })
        return entries
    }

    visible: false
    anchors.fill: parent
    z: 900   // Above AccountCenter; below the later onboarding host.

    property real anchorRight: -1
    property real anchorBottom: -1
    property int edgeMargin: 16
    property int anchorGap: 8

    Theme { id: theme }

    function syncLine() {
        if (!controller)
            return ""
        if (controller.syncRetryAvailable)
            return qsTr("Sync needs attention")
        if (controller.pendingOutboxCount === 1)
            return qsTr("Syncing — 1 change pending")
        if (controller.pendingOutboxCount > 1)
            return qsTr("Syncing — %1 changes pending").arg(controller.pendingOutboxCount)
        if (controller.syncState === "blocked")
            return qsTr("Sync needs attention")
        if (controller.syncState === "retrying")
            return qsTr("Waiting for the account service…")
        if (controller.syncState === "inactive")
            return qsTr("Cloud sync off")
        if (controller.mode === "offline")
            return qsTr("Offline")
        return qsTr("All changes synced")
    }

    function focusItem(target) {
        if (target && target.visible && target.enabled)
            target.forceActiveFocus()
    }

    function open() {
        if (!visible) {
            const active = root.Window.window ? root.Window.window.activeFocusItem : null
            if (active && !SystemFocusContainment.isWithin(active, root))
                focusReturnItem = active
        }
        visible = true
        Qt.callLater(function() {
            if (root.visible)
                root.focusItem(actions.itemAt(0))
        })
    }

    function close(restoreFocus) {
        if (!visible)
            return
        visible = false
        const target = focusReturnItem
        focusReturnItem = null
        if (restoreFocus !== false)
            Qt.callLater(function() { root.focusItem(target) })
    }

    function toggle() { visible ? close() : open() }
    function openAt(right, bottom) {
        anchorRight = right
        anchorBottom = bottom
        open()
    }
    function toggleAt(right, bottom) {
        anchorRight = right
        anchorBottom = bottom
        toggle()
    }

    function activate(section) {
        if (section === "retry") {
            controller.retrySync()
            return
        }
        const invoker = focusReturnItem
        close(false)
        switch (section) {
        case "signIn":
            focusItem(invoker)
            controller.returnToSignIn()
            break
        case "signOut":
            focusItem(invoker)
            controller.logoutCurrent()
            break
        case "theatre":
            theatreSyncRequested(invoker)
            break
        default:
            if (accountSettings)
                accountSettings.open(section, invoker)
            break
        }
    }

    function moveDirectional(step) {
        const active = root.Window.window ? root.Window.window.activeFocusItem : null
        for (let i = 0; i < actions.count; ++i) {
            if (actions.itemAt(i) === active) {
                const next = i + step
                if (next >= 0 && next < actions.count)
                    focusItem(actions.itemAt(next))
                return
            }
        }
        if (actions.count)
            focusItem(actions.itemAt(step > 0 ? 0 : actions.count - 1))
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
            event.accepted = true
        } else if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                   && event.modifiers === Qt.NoModifier) {
            root.moveDirectional(event.key === Qt.Key_Down ? 1 : -1)
            event.accepted = true
        } else if (event.key === Qt.Key_Tab) {
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

    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    Rectangle {
        id: card
        readonly property real rightEdge: root.anchorRight >= 0 ? root.anchorRight : root.width - 58
        x: Math.max(root.edgeMargin, Math.min(root.width - width - root.edgeMargin, rightEdge - width))
        y: Math.max(root.edgeMargin, Math.min(root.height - height - root.edgeMargin,
            root.anchorBottom >= 0 ? root.anchorBottom + root.anchorGap : 66))
        width: Math.min(312, Math.max(0, root.width - 2 * root.edgeMargin))
        height: content.implicitHeight + 32
        radius: 18
        color: Qt.rgba(0.08, 0.085, 0.105, 0.97)
        border.color: theme.edge

        // Empty space inside the card isn't an outside click.
        MouseArea { anchors.fill: parent }

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 12

            RowLayout {
                width: parent.width
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Layout.alignment: Qt.AlignVCenter
                    radius: width / 2
                    color: theme.glassTint
                    border.color: theme.edge

                    Image {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        fillMode: Image.PreserveAspectFit
                        source: "../../assets/icons/colosseum.svg"
                        visible: !root.accountPresent
                    }

                    AccountAvatarGlyph {
                        anchors.centerIn: parent
                        width: 23
                        height: 23
                        visible: root.accountPresent
                        avatarId: root.controller && root.controller.avatarId || "initial"
                        initial: root.initial
                        strokeColor: theme.gold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        objectName: "accountFlyoutUsername"
                        text: root.accountPresent ? root.controller.username : qsTr("Colosseum account")
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.accountPresent ? root.syncLine() : qsTr("Using Colosseum on this device")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4

                Repeater {
                    id: actions
                    model: root.menuEntries

                    Button {
                        id: action
                        required property var modelData
                        readonly property string section: modelData.section
                        readonly property bool primary: section === "signIn"
                        readonly property bool theatre: section === "theatre"
                        readonly property bool opensPage: section === "profile" || section === "devices"
                            || section === "privacy" || theatre
                        objectName: section === "signIn" || section === "signOut" ? "accountFlyoutSessionAction"
                            : section === "retry" ? "accountFlyoutSyncRetry" : "accountFlyoutNav_" + section
                        width: parent.width
                        height: 39
                        leftPadding: 12
                        rightPadding: 12
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        text: modelData.label
                        Accessible.name: text
                        Keys.onReturnPressed: clicked()
                        Keys.onEnterPressed: clicked()
                        onClicked: root.activate(section)

                        background: Rectangle {
                            radius: 9
                            color: action.primary
                                ? (action.down ? Qt.darker(theme.gold, 1.12) : theme.gold)
                                : action.hovered || action.down ? theme.glassTint : "transparent"
                            border.width: action.activeFocus ? 2 : 0
                            border.color: action.primary ? theme.ink : theme.gold
                        }

                        contentItem: RowLayout {
                            spacing: 7
                            Text {
                                Layout.fillWidth: true
                                text: action.text
                                color: action.primary ? "#141207" : theme.ink
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: action.primary ? Font.DemiBold : Font.Normal
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            Image {
                                visible: action.theatre && root.stremioAvailable
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: "../../assets/icons/stremio-official.svg"
                                fillMode: Image.PreserveAspectFit
                            }
                            Image {
                                visible: action.theatre && root.nuvioAvailable
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: "../../assets/icons/nuvio-official.png"
                                fillMode: Image.PreserveAspectFit
                            }
                            Text {
                                visible: action.opensPage
                                text: "›"
                                color: theme.inkDimmer
                                font.pixelSize: 16
                            }
                        }
                    }
                }
            }
        }
    }
}

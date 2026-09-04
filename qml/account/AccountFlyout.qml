// Account identity flyout (Bundle 8C first-light): drops from the topbar
// medallion. Deliberately NOT a Popup — modal Popup overlays fight Qt's
// grab semantics on this shell; a plain layer with an explicit full-screen
// click-catcher closes on any outside tap, deterministically.
import QtQuick
import QtQuick.Controls
import ".."
import "../SystemFocusContainment.js" as SystemFocusContainment

Item {
    id: root

    property var controller: null
    property string initial: "?"
    property Item focusReturnItem: null
    readonly property bool accountPresent: controller
        && (controller.mode === "signedIn" || controller.mode === "offline")
    readonly property bool onlineAccount: controller
        && controller.mode === "signedIn"

    visible: false
    anchors.fill: parent
    z: 900   // above AccountCenter by document order; below the onboarding host (900), which is instantiated after this in Main.qml

    property real anchorRight: -1
    property real anchorBottom: -1
    property int edgeMargin: 16
    property int anchorGap: 8

    function syncLine() {
        if (!controller)
            return "";
        if (controller.pendingOutboxCount > 0)
            return qsTr("Syncing — %1 change%2 pending")
                .arg(controller.pendingOutboxCount)
                .arg(controller.pendingOutboxCount === 1 ? "" : "s");
        if (controller.syncState === "blocked")
            return qsTr("Sync needs attention");
        if (controller.syncState === "retrying")
            return qsTr("Waiting for the account service…");
        if (controller.syncState === "inactive")
            return qsTr("Cloud sync off");
        return qsTr("All changes synced");
    }

    // Full-screen click-catcher: any tap outside the card closes.
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    function rememberInvoker() {
        const active = root.Window.window ? root.Window.window.activeFocusItem : null
        if (active && !SystemFocusContainment.isWithin(active, root))
            focusReturnItem = active
    }

    function focusInitial() {
        Qt.callLater(function() {
            if (!root.visible)
                return
            if (root.accountPresent && navRepeater.count > 0) {
                const first = navRepeater.itemAt(0)
                if (first && first.visible && first.enabled) {
                    first.forceActiveFocus()
                    return
                }
            }
            sessionAction.forceActiveFocus()
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

    function open() {
        if (!root.visible)
            rememberInvoker()
        root.visible = true
        focusInitial()
    }
    function close(restoreFocus) {
        if (!root.visible)
            return
        root.visible = false
        if (restoreFocus !== false)
            restoreInvoker()
    }
    function toggle() { root.visible ? root.close() : root.open() }
    function openAt(right, bottom) {
        root.anchorRight = right
        root.anchorBottom = bottom
        root.open()
    }
    function toggleAt(right, bottom) {
        root.anchorRight = right
        root.anchorBottom = bottom
        root.toggle()
    }

    function openCentre(section) {
        const invoker = focusReturnItem
        focusReturnItem = null
        root.close(false)
        if (typeof accountCenter !== "undefined" && accountCenter)
            accountCenter.open(section, invoker)
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
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

    Rectangle {
        id: card
        x: root.anchorRight >= 0
           ? Math.max(root.edgeMargin, Math.min(root.width - width - root.edgeMargin, root.anchorRight - width))
           : parent.width - width - 58
        y: root.anchorBottom >= 0
           ? Math.max(root.edgeMargin, Math.min(root.height - height - root.edgeMargin, root.anchorBottom + root.anchorGap))
           : 66
        width: 296
        height: col.implicitHeight + 44
        radius: 16
        color: "#15130f"
        border.width: 1
        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.28)
        Rectangle {           // soft gold sheen along the top edge
            width: parent.width - 40; height: 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top; anchors.topMargin: 1
            color: Qt.rgba(0.94, 0.77, 0.29, 0.55)
        }

        Column {
            id: col
            x: 20; y: 22
            width: parent.width - 40
            spacing: 14

            Row {
                spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: 20
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                    border.width: 1.5
                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                    Text {
                        anchors.centerIn: parent
                        text: root.initial
                        color: "#f0df9a"
                        font.family: "Inter"
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                }
                Column {
                    spacing: 2
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        objectName: "accountFlyoutUsername"
                        text: root.accountPresent
                              ? (controller ? controller.username : "")
                              : qsTr("Not signed in")
                        color: "#f2f2ef"
                        font.family: "Inter"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Colosseum account")
                        color: "#8f8b80"
                        font.family: "Inter"
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                width: parent.width; height: 1; color: "#26231d"
                visible: root.accountPresent
            }

            Row {
                spacing: 8
                width: parent.width
                visible: root.accountPresent
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: {
                        if (!controller) return "#8f8b80";
                        if (controller.syncState === "blocked") return "#e0564b";
                        if (controller.pendingOutboxCount > 0
                            || controller.syncState === "retrying")
                            return "#f0df9a";
                        return "#7ec97e";
                    }
                }
                Text {
                    width: parent.width - 15
                    text: root.syncLine()
                    color: "#b7b3a6"
                    font.family: "Inter"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            // nav into the centre (Preflight menu mock, merged with identity)
            Rectangle {
                width: parent.width; height: 1; color: "#26231d"
                visible: root.accountPresent
            }

            Column {
                id: navColumn
                width: parent.width
                spacing: 2
                visible: root.accountPresent
                Repeater {
                    id: navRepeater
                    model: [
                        { label: qsTr("Account ›"), section: "colosseum" },
                        { label: qsTr("Devices ›"), section: "devices" }
                    ]
                    Item {
                        id: navItem
                        width: parent ? parent.width : 0
                        height: 32
                        activeFocusOnTab: root.visible && navColumn.visible
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                        Keys.onReturnPressed: root.openCentre(modelData.section)
                        Keys.onEnterPressed: root.openCentre(modelData.section)
                        Keys.onSpacePressed: root.openCentre(modelData.section)
                        Keys.onPressed: function(event) {
                            if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
                                return
                            const next = index + (event.key === Qt.Key_Down ? 1 : -1)
                            if (next >= 0 && next < navRepeater.count) {
                                const candidate = navRepeater.itemAt(next)
                                if (candidate) {
                                    candidate.forceActiveFocus()
                                    event.accepted = true
                                }
                            } else if (event.key === Qt.Key_Down) {
                                sessionAction.forceActiveFocus()
                                event.accepted = true
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            visible: navItem.activeFocus
                            color: Qt.rgba(0.94, 0.77, 0.29, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(0.94, 0.77, 0.29, 0.72)
                        }

                        Text {
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: navItem.activeFocus || navMa.containsMouse ? "#f2f2ef" : "#b7b3a6"
                            font.family: "Inter"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openCentre(modelData.section)
                        }
                    }
                }
            }

            Button {
                id: sessionAction
                objectName: "accountFlyoutSessionAction"
                width: parent.width
                height: 38
                focusPolicy: Qt.StrongFocus
                Keys.onUpPressed: {
                    if (root.accountPresent && navRepeater.count > 0) {
                        const last = navRepeater.itemAt(navRepeater.count - 1)
                        if (last)
                            last.forceActiveFocus()
                    }
                }
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: sessionAction.activeFocus ? 2 : 1
                    border.color: sessionAction.activeFocus
                        ? "#f0df9a"
                        : (parent.hovered ? "#3a362c" : "#2a2720")
                }
                contentItem: Text {
                    text: root.accountPresent ? qsTr("Sign out") : qsTr("Sign in")
                    color: "#d8d4c8"
                    font.family: "Inter"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (root.controller) {
                        if (root.accountPresent)
                            root.controller.logoutCurrent();
                        else
                            root.controller.returnToSignIn();
                    }
                    root.close();
                }
            }
        }
    }
}

// Account Centre (Bundle 8C, built to the Preflight 18-mock frame + Agent 0's
// amendments): full-screen glass page in the Updates/Wallpapers trinity.
// Left rail: Profile · Security · Devices · Recovery · Data & Privacy.
// Opens on the sync hero the mocks lacked — the library the account protects.
import QtQuick
import QtQuick.Controls
import ".."

Rectangle {
    id: root

    property var controller: null
    property string initial: "?"
    property string activeSection: "library"

    visible: false
    anchors.fill: parent
    z: 898   // under the onboarding host + flyout; above all chrome
    color: "#0d0c09"

    Behavior on opacity { NumberAnimation { duration: 160 } }
    onVisibleChanged: if (visible) opacity = 1; else opacity = 0
    opacity: 0

    function open(section) {
        if (section) activeSection = section;
        root.visible = true;
    }
    function close() { root.visible = false }

    // Full-screen click layer is NOT used here (it's a page, not a popup) —
    // Escape and the Back row close it.
    Keys.onEscapePressed: root.close()

    // ── dim the shell behind ──
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.45
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // ── left rail ──
        Rectangle {
            width: 232
            height: parent.height
            color: "#121009"

            Column {
                x: 20; y: 74
                width: parent.width - 40
                spacing: 4

                // identity stamp
                Row {
                    spacing: 12
                    bottomPadding: 18
                    Rectangle {
                        width: 38; height: 38; radius: 19
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                        border.width: 1.5
                        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                        Text {
                            anchors.centerIn: parent
                            text: root.initial
                            color: "#f0df9a"
                            font.family: "Inter"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                    }
                    Column {
                        spacing: 1
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: controller ? controller.username : ""
                            color: "#f2f2ef"
                            font.family: "Inter"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("Colosseum account")
                            color: "#7d7a6f"
                            font.family: "Inter"
                            font.pixelSize: 10
                        }
                    }
                }

                Repeater {
                    model: [
                        { id: "library",  label: qsTr("Your library"), glyph: "◍" },
                        { id: "profile",  label: qsTr("Profile"),      glyph: "◌" },
                        { id: "security", label: qsTr("Security"),     glyph: "◇" },
                        { id: "devices",  label: qsTr("Devices"),      glyph: "▣" },
                        { id: "recovery", label: qsTr("Recovery"),     glyph: "↶" },
                        { id: "privacy",  label: qsTr("Data & privacy"), glyph: "◫" }
                    ]
                    Rectangle {
                        width: parent ? parent.width : 0
                        height: 38
                        radius: 9
                        color: root.activeSection === modelData.id
                               ? Qt.rgba(0.94, 0.77, 0.29, 0.12)
                               : (railMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                        Row {
                            x: 12; spacing: 10
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: modelData.glyph
                                color: root.activeSection === modelData.id ? "#f0df9a" : "#8f8b80"
                                font.pixelSize: 13
                            }
                            Text {
                                text: modelData.label
                                color: root.activeSection === modelData.id ? "#f2f2ef" : "#b7b3a6"
                                font.family: "Inter"
                                font.pixelSize: 13
                                font.weight: root.activeSection === modelData.id ? Font.DemiBold : Font.Normal
                            }
                        }
                        MouseArea {
                            id: railMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.activeSection = modelData.id
                        }
                    }
                }

                Item { width: 1; height: 14 }

                // quiet sign-out at the rail's foot
                Text {
                    text: qsTr("Sign out")
                    color: "#8f8b80"
                    font.family: "Inter"
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: parent.color = railOutMa.containsMouse ? "#d8d4c8" : "#8f8b80"
                        id: railOutMa
                        onClicked: {
                            if (root.controller)
                                root.controller.logoutCurrent();
                            root.close();
                        }
                    }
                }
            }
            Rectangle {  // rail's right hairline
                width: 1; height: parent.height
                anchors.right: parent.right
                color: "#221f18"
            }
        }

        // ── content column ──
        Item {
            width: parent.width - 232
            height: parent.height

            // Back row (house pattern)
            Text {
                x: 34; y: 30
                text: qsTr("‹ Back")
                color: "#8f8b80"
                font.family: "Inter"
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }

            // ═══ LIBRARY — the hero the mocks lacked ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 18
                visible: root.activeSection === "library"

                Text {
                    text: qsTr("Your library, everywhere")
                    color: "#f2f2ef"
                    font.family: "Inter"
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: controller
                          ? qsTr("Signed in as %1 — your Continue shelf, Collection and history follow you to every device.")
                            .arg(controller.username)
                          : ""
                    color: "#8f8b80"
                    font.family: "Inter"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                // sync truth line
                Row {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: {
                            if (!controller) return "#8f8b80";
                            if (controller.syncState === "blocked") return "#e0564b";
                            if (controller.pendingOutboxCount > 0
                                || controller.syncState === "retrying") return "#f0df9a";
                            return "#7ec97e";
                        }
                    }
                    Text {
                        text: {
                            if (!controller) return "";
                            if (controller.pendingOutboxCount > 0)
                                return qsTr("Syncing — %1 pending").arg(controller.pendingOutboxCount);
                            if (controller.syncState === "blocked") return qsTr("Sync needs attention");
                            if (controller.syncState === "retrying") return qsTr("Waiting for the account service…");
                            return qsTr("All changes synced");
                        }
                        color: "#b7b3a6"
                        font.family: "Inter"
                        font.pixelSize: 12
                    }
                }

                // protected-worlds tiles (counts from the sync service data)
                Row {
                    spacing: 14
                    Repeater {
                        model: [
                            { world: qsTr("Continue"), note: qsTr("resume points") },
                            { world: qsTr("Collection"), note: qsTr("saved shelf") },
                            { world: qsTr("History"), note: qsTr("completed") },
                            { world: qsTr("Preferences"), note: qsTr("profile-wide") }
                        ]
                        Rectangle {
                            width: 168; height: 92
                            radius: 13
                            color: "#15130e"
                            border.width: 1
                            border.color: "#242019"
                            Column {
                                x: 16; y: 14
                                spacing: 4
                                Text {
                                    text: modelData.world
                                    color: "#b7b3a6"
                                    font.family: "Inter"
                                    font.pixelSize: 11
                                }
                                Text {
                                    text: modelData.note
                                    color: "#5f5c53"
                                    font.family: "Inter"
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("Search history, window state and machine paths never leave this device — by design.")
                    color: "#5f5c53"
                    font.family: "Inter"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    width: parent.width
                }
            }

            // ═══ PROFILE ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 16
                visible: root.activeSection === "profile"

                Text {
                    text: qsTr("Profile")
                    color: "#f2f2ef"; font.family: "Inter"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                }
                Text {
                    text: controller ? qsTr("Username: %1").arg(controller.username) : ""
                    color: "#b7b3a6"; font.family: "Inter"; font.pixelSize: 13
                }
                Text {
                    text: qsTr("Avatar and username changes arrive with this page's next pass.")
                    color: "#5f5c53"; font.family: "Inter"; font.pixelSize: 11
                }
            }

            // ═══ SECURITY ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 16
                visible: root.activeSection === "security"

                Text {
                    text: qsTr("Security")
                    color: "#f2f2ef"; font.family: "Inter"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Password change and new-device protection arrive with this page's next pass.")
                    color: "#5f5c53"; font.family: "Inter"; font.pixelSize: 11
                }
            }

            // ═══ DEVICES ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 16
                visible: root.activeSection === "devices"

                Text {
                    text: qsTr("Devices")
                    color: "#f2f2ef"; font.family: "Inter"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                }
                Text {
                    text: controller
                          ? qsTr("%1 device%2 trusted on your account.")
                            .arg(controller.deviceCount)
                            .arg(controller.deviceCount === 1 ? "" : "s")
                          : ""
                    color: "#b7b3a6"; font.family: "Inter"; font.pixelSize: 13
                }
                Column {
                    spacing: 10
                    Repeater {
                        model: controller ? controller.devices : []
                        Rectangle {
                            width: parent ? parent.width : 0
                            height: 58
                            radius: 12
                            color: "#15130e"
                            border.width: 1
                            border.color: (controller && modelData.install_id === controller.deviceId)
                                          ? Qt.rgba(0.94, 0.77, 0.29, 0.4) : "#242019"
                            Column {
                                x: 16; y: 11; spacing: 3
                                Text {
                                    text: (modelData.label || qsTr("Unnamed device"))
                                          + ((controller && modelData.install_id === controller.deviceId)
                                             ? qsTr("  ·  this device") : "")
                                    color: "#e8e4d8"; font.family: "Inter"
                                    font.pixelSize: 13; font.weight: Font.DemiBold
                                }
                                Text {
                                    text: qsTr("%1 · last seen %2")
                                        .arg(modelData.platform || "—")
                                        .arg(modelData.last_seen_at || "—")
                                    color: "#7d7a6f"; font.family: "Inter"; font.pixelSize: 11
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !(controller && modelData.install_id === controller.deviceId)
                                text: qsTr("Revoke")
                                color: "#e0564b"; font.family: "Inter"
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                MouseArea {
                                    anchors.fill: parent; anchors.margins: -10
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (controller) controller.revokeDevice(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            // ═══ RECOVERY ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 16
                visible: root.activeSection === "recovery"

                Text {
                    text: qsTr("Recovery")
                    color: "#f2f2ef"; font.family: "Inter"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: qsTr("Your recovery key is the one way back into this account if you ever forget your password. It was shown once when the account was made. Replacing it needs your current password.")
                    color: "#8f8b80"; font.family: "Inter"; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    text: qsTr("Key replacement arrives with this page's next pass.")
                    color: "#5f5c53"; font.family: "Inter"; font.pixelSize: 11
                }
            }

            // ═══ DATA & PRIVACY ═══
            Column {
                x: 34; y: 64
                width: parent.width - 68
                spacing: 16
                visible: root.activeSection === "privacy"

                Text {
                    text: qsTr("Data & privacy")
                    color: "#f2f2ef"; font.family: "Inter"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: qsTr("What syncs: Continue progress, your Collection shelf, completed history, and profile preferences — encrypted end to end.\n\nWhat never leaves this device: search history, window state, file paths, media itself.\n\nAccount deletion arrives with the service's deletion slice; nothing here removes data today.")
                    color: "#8f8b80"; font.family: "Inter"; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}

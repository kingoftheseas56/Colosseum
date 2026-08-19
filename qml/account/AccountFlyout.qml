// Account identity flyout (Bundle 8C first-light): drops from the topbar
// medallion. Deliberately NOT a Popup — modal Popup overlays fight Qt's
// grab semantics on this shell; a plain layer with an explicit full-screen
// click-catcher closes on any outside tap, deterministically.
import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property var controller: null
    property string initial: "?"
    readonly property bool signedIn:
        controller && controller.mode === "signedIn"

    visible: false
    anchors.fill: parent
    z: 900   // above AccountCenter by document order; below the onboarding host (900), which is instantiated after this in Main.qml

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

    function open() { root.visible = true }
    function close() { root.visible = false }
    function toggle() { root.visible = !root.visible }

    Rectangle {
        id: card
        x: parent.width - width - 58
        y: 66
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
                        text: root.signedIn
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
                visible: root.signedIn
            }

            Row {
                spacing: 8
                width: parent.width
                visible: root.signedIn
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
                visible: root.signedIn
            }

            Column {
                width: parent.width
                spacing: 2
                visible: root.signedIn
                Repeater {
                    model: [
                        { label: qsTr("Account ›"), section: "colosseum" },
                        { label: qsTr("Devices ›"), section: "devices" }
                    ]
                    Item {
                        width: parent ? parent.width : 0
                        height: 32
                        Text {
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: navMa.containsMouse ? "#f2f2ef" : "#b7b3a6"
                            font.family: "Inter"
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.close();
                                if (typeof accountCenter !== "undefined"
                                    && accountCenter)
                                    accountCenter.open(modelData.section);
                            }
                        }
                    }
                }
            }

            Button {
                width: parent.width
                height: 38
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: parent.hovered ? "#3a362c" : "#2a2720"
                }
                contentItem: Text {
                    text: root.signedIn ? qsTr("Sign out") : qsTr("Sign in")
                    color: "#d8d4c8"
                    font.family: "Inter"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (root.controller) {
                        if (root.signedIn)
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

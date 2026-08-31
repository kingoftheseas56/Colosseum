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
    readonly property bool accountPresent: controller
        && (controller.mode === "signedIn" || controller.mode === "offline")
    readonly property bool onlineAccount: controller
        && controller.mode === "signedIn"
    readonly property bool localOnly: controller
        && controller.mode === "localOnly"

    property real anchorRight: -1
    property real anchorBottom: -1
    property int edgeMargin: 16
    property int anchorGap: 8

    signal signInRequested()
    signal createAccountRequested()
    signal yourColosseumRequested()
    signal privacyRequested()

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
    function openAt(right, bottom) {
        root.anchorRight = right
        root.anchorBottom = bottom
        root.visible = true
    }
    function toggleAt(right, bottom) {
        root.anchorRight = right
        root.anchorBottom = bottom
        root.visible = !root.visible
    }

    Rectangle {
        id: card
        x: root.anchorRight >= 0
            ? Math.max(root.edgeMargin,
                       Math.min(root.width - width - root.edgeMargin,
                                root.anchorRight - width))
            : parent.width - width - 58
        y: root.anchorBottom >= 0
            ? Math.max(root.edgeMargin,
                       Math.min(root.height - height - root.edgeMargin,
                                root.anchorBottom + root.anchorGap))
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
                objectName: root.localOnly ? "accountFlyoutLocalIdentity" : ""
                spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: 20
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                    border.width: 1.5
                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                    Text {
                        anchors.centerIn: parent
                        text: root.localOnly ? "D" : root.initial
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
                        objectName: root.localOnly
                            ? "accountFlyoutLocalDeviceLabel"
                            : "accountFlyoutUsername"
                        text: root.localOnly && root.controller
                            ? root.controller.localDeviceLabel
                            : (root.accountPresent
                                ? (root.controller ? root.controller.username : "")
                                : qsTr("Not signed in"))
                        color: "#f2f2ef"
                        font.family: "Inter"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.localOnly
                            ? qsTr("Local Colosseum · this device")
                            : qsTr("Colosseum account")
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
                width: parent.width
                spacing: 2
                visible: root.accountPresent
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

            Column {
                width: parent.width
                spacing: 2
                visible: root.localOnly

                Button {
                    id: localYourColosseumButton
                    objectName: "accountFlyoutLocalYourColosseum"
                    width: parent.width
                    height: 34
                    padding: 0
                    Accessible.name: qsTr("Your Colosseum")
                    background: Rectangle {
                        radius: 8
                        color: localYourColosseumButton.hovered
                            ? Qt.rgba(1, 1, 1, 0.06)
                            : "transparent"
                    }
                    contentItem: Text {
                        text: qsTr("Your Colosseum")
                        color: localYourColosseumButton.hovered ? "#f2f2ef" : "#b7b3a6"
                        font.family: "Inter"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        root.close()
                        root.yourColosseumRequested()
                    }
                }

                Button {
                    id: localPrivacyButton
                    objectName: "accountFlyoutLocalPrivacy"
                    width: parent.width
                    height: 34
                    padding: 0
                    Accessible.name: qsTr("Data & privacy")
                    background: Rectangle {
                        radius: 8
                        color: localPrivacyButton.hovered
                            ? Qt.rgba(1, 1, 1, 0.06)
                            : "transparent"
                    }
                    contentItem: Text {
                        text: qsTr("Data & privacy")
                        color: localPrivacyButton.hovered ? "#f2f2ef" : "#b7b3a6"
                        font.family: "Inter"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        root.close()
                        root.privacyRequested()
                    }
                }

                Item { width: 1; height: 6 }
                Rectangle { width: parent.width; height: 1; color: "#26231d" }
                Item { width: 1; height: 8 }

                Row {
                    width: parent.width
                    spacing: 8

                    AccountButton {
                        objectName: "accountFlyoutLocalSignIn"
                        width: (parent.width - parent.spacing) / 2
                        height: 36
                        text: qsTr("Sign in")
                        variant: "primary"
                        Accessible.name: qsTr("Sign in")
                        onClicked: {
                            root.close()
                            root.signInRequested()
                        }
                    }

                    AccountButton {
                        objectName: "accountFlyoutLocalCreateAccount"
                        width: (parent.width - parent.spacing) / 2
                        height: 36
                        text: qsTr("Create account")
                        Accessible.name: qsTr("Create account")
                        onClicked: {
                            root.close()
                            root.createAccountRequested()
                        }
                    }
                }
            }

            Button {
                objectName: "accountFlyoutSessionAction"
                visible: !root.localOnly
                width: parent.width
                height: 38
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: parent.hovered ? "#3a362c" : "#2a2720"
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

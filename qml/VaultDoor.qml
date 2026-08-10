// VaultDoor.qml — the taskbar's permanent folder door: the always-present "On this machine"
// entry (Slice 10) plus the alive-door state (Slice 15): a quiet gold dot while any scan runs,
// and a time-boxed "arrival" glow the moment a dropped file lands on a live shelf. No counts,
// no badges (spec §3) — the door only pulses and glows.
//
// The state machine lives HERE (QML-side) so the Qt Quick Test can seed scanning/arrivalTick
// and assert the doorState sequence (idle → scanning → arrival-pulse → idle); the FACTS come
// from VaultLibrary (scanning + the monotone arrivalTick landing clock) wired by Taskbar.
// QML paints, C++ decides: C++ only ever bumps arrivalTick / toggles scanning.

import QtQuick

Item {
    id: door
    objectName: "taskbarVaultDoor"

    // ── inputs (wired by Taskbar from VaultLibrary; seeded by tests) ──
    property bool active: false       // the Vault page is the front surface (gold underline)
    property bool scanning: false     // a census/publish is running → the quiet gold dot
    property int arrivalTick: 0       // monotone landing clock; each bump starts the pulse
    signal clicked()

    // ── the door state machine ──
    property bool arrivalPulse: false
    readonly property string doorState: arrivalPulse ? "arrival"
                                          : (scanning ? "scanning" : "idle")
    onArrivalTickChanged: if (arrivalTick > 0) { arrivalPulse = true; pulseTimer.restart() }
    Timer {
        id: pulseTimer
        interval: 2400   // ≥ 2s, partly FOR observability (50ms Lanista polls must catch it)
        onTriggered: arrivalPulse = false
    }

    Rectangle {
        id: doorTile
        anchors.fill: parent
        radius: 13
        color: doorMa.containsMouse || door.active ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)

        // arrival glow: a brief gold ring around the tile
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
            opacity: door.arrivalPulse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }
    }

    Image {
        anchors.centerIn: parent
        width: 21; height: 21
        source: "../assets/icons/vault-folder.svg"
        fillMode: Image.PreserveAspectFit
        opacity: door.active ? 1 : 0.75
    }

    // active-page underline, same gold language as session tiles
    Rectangle {
        visible: door.active
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
        width: 20; height: 3; radius: 2
        color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
    }

    // the quiet gold dot while a scan runs (named for the Lanista probe + grab)
    Rectangle {
        objectName: "vaultDoorScanDot"
        visible: door.scanning
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.rightMargin: 7; anchors.bottomMargin: 7
        width: 5; height: 5; radius: 2.5
        color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
    }

    MouseArea {
        id: doorMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: door.clicked()
    }
}

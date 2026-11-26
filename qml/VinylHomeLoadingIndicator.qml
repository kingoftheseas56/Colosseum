import QtQuick
import QtQuick.Controls

// Shared face for the host's continuous loader and the in-page loading/Retry state.
// Vinyl's loader is a record on the platter (Hemanth, 2026-11-27): the label turns at 33 1/3 rpm
// under a tonearm that swings onto the record while music loads and lifts off when loading fails.
// The grooves and their sheen stay still, as they do on a real turntable; only the label visibly turns.
Column {
    id: root
    property bool indeterminate: true
    property bool showIndicator: true
    property string error: ""
    // Home is ready: the needle drops into the groove, then the record fades into Home (~0.78 s;
    // the host keeps this loader up for that beat).
    property bool dropping: false
    opacity: 1
    SequentialAnimation {
        running: root.dropping
        PauseAnimation { duration: 430 }
        NumberAnimation { target: root; property: "opacity"; to: 0; duration: 320; easing.type: Easing.InCubic }
    }
    onDroppingChanged: if (!dropping) opacity = 1
    width: Math.min(420, parent.width)
    x: (parent.width - width) / 2
    spacing: 18

    Theme { id: theme }

    Item {
        id: turntable
        // Historical automation name: tests and Lanista address the loader by it.
        objectName: "vinylHomeLoadingBar"
        readonly property bool indeterminate: root.indeterminate
        readonly property color labelColor: theme.gold
        readonly property bool spinning: visible && (root.indeterminate || root.dropping)
        visible: root.showIndicator
        width: 136
        height: 120
        x: (root.width - width) / 2
        Accessible.role: Accessible.ProgressBar
        Accessible.name: qsTr("Loading your music")

        Item {
            id: disc
            width: 120
            height: 120
            Rectangle { anchors.fill: parent; radius: width / 2; color: "#09090d" }
            // Inner grooves between the label and the outer band.
            Repeater {
                model: [0.62, 0.55, 0.48, 0.41]
                Rectangle {
                    required property real modelData
                    width: disc.width * modelData
                    height: width
                    anchors.centerIn: parent
                    radius: width / 2
                    color: "transparent"
                    border.color: Qt.rgba(1, 1, 1, 0.07)
                    border.width: 1
                }
            }
            // The house record band (the same art as the artist records).
            Image {
                anchors.fill: parent
                source: "vinyl/assets/record-ring.svg"
                sourceSize: Qt.size(256, 256)
                smooth: true
            }
            Item {
                id: label
                width: disc.width * 0.34
                height: width
                anchors.centerIn: parent
                Rectangle { anchors.fill: parent; radius: width / 2; color: turntable.labelColor }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.62; height: width; radius: width / 2
                    color: "transparent"; border.color: theme.vaultGoldInk; border.width: 1; opacity: 0.35
                }
                // A mark on the label, so the turn reads.
                Rectangle {
                    width: 3; height: parent.height * 0.22; radius: 1.5
                    x: (parent.width - width) / 2; y: parent.height * 0.1
                    color: theme.vaultGoldInk; opacity: 0.6
                }
                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5; color: "#09090d" }
                RotationAnimator on rotation {
                    from: 0; to: 360
                    duration: 1800  // 33 1/3 rpm
                    loops: Animation.Infinite
                    running: turntable.spinning
                }
            }
        }

        // Tonearm: pivot top right; swings onto the record while loading, lifts off on failure.
        Item {
            id: tonearm
            x: disc.width + 2
            y: 2
            width: 1
            height: 1
            Rectangle {
                id: arm
                x: -1.5
                y: 0
                width: 3
                height: 64
                radius: 1.5
                color: theme.inkDimmer
                transformOrigin: Item.Top
                rotation: root.dropping ? 31 : tonearm.resting ? 24 : -14
                Behavior on rotation { NumberAnimation { duration: root.dropping ? 380 : 650; easing.type: root.dropping ? Easing.OutBack : Easing.OutCubic } }
                Rectangle {   // headshell
                    width: 9; height: 13; radius: 2
                    x: (parent.width - width) / 2; y: parent.height - 4
                    color: theme.inkDim
                }
            }
            Rectangle {   // pivot
                width: 14; height: 14; radius: 7
                x: -7; y: -7
                color: "#2a2c36"; border.color: theme.inkDimmer; border.width: 1
            }
            // Start lifted, then swing in, so the arm visibly settles onto the record.
            property bool settled: false
            readonly property bool resting: settled && (root.indeterminate || root.dropping)
            Component.onCompleted: Qt.callLater(function() { tonearm.settled = true })
        }
    }

    Text {
        objectName: "vinylHomeLoadingMessage"
        visible: root.showIndicator
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.indeterminate || root.dropping ? qsTr("Loading your music…") : root.error || qsTr("Could not load your music.")
        color: theme.inkDim
        font.family: theme.display
        font.pixelSize: 16
    }
}

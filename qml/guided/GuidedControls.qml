// GuidedControls — the transport for the Guided Reader (fifth style): Panel Step,
// Auto Read (play/pause), reading speed, a live analysis status line, and Exit Guided.
// It only DRIVES the shared GuidedCameraController and reports Exit; the controller owns
// all camera state. When a manual gesture has interrupted Auto Read, the whole bar
// collapses to a single "Resume Auto Read" — the one thing to do next.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/guided/)

Item {
    id: root

    property var controller: null          // GuidedCameraController (native, or a mock in tests)
    property var analysis: ({})            // GuidedAnalysis.jobSummary(entryId)
    property bool resumeOnly: false         // interrupted → collapse to Resume Auto Read
    signal exitRequested()

    implicitWidth: bar.width
    implicitHeight: bar.height

    Theme { id: theme }

    readonly property bool _auto: controller ? controller.autoRead : false
    readonly property real _speed: controller ? controller.speed : 1.0

    function _clampSpeed(s) { return Math.max(0.5, Math.min(2.0, s)) }

    // A small glass pill; `primary` fills gold for the one call-to-action.
    component Pill: Rectangle {
        id: pill
        property string label: ""
        property bool primary: false
        signal activated()
        implicitWidth: pillText.implicitWidth + 26
        implicitHeight: 30
        radius: 8
        color: primary ? (pillMa.containsMouse ? Qt.lighter(theme.gold, 1.08) : theme.gold)
                        : (pillMa.containsMouse ? theme.glassHi : theme.glassTint)
        border.width: primary ? 0 : 1
        border.color: theme.edge
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: pill.primary ? "#1a1306" : theme.ink
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: pill.primary ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            id: pillMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.activated()
        }
    }

    Rectangle {
        id: bar
        anchors.centerIn: parent
        width: content.implicitWidth + 28
        height: 44
        radius: 12
        color: "#e015171f"
        border.width: 1
        border.color: theme.edge

        // --- interrupted: one clear next action ---
        Pill {
            visible: root.resumeOnly
            anchors.centerIn: parent
            label: "Resume Auto Read"
            primary: true
            onActivated: if (root.controller) root.controller.resumeAutoRead()
        }

        // --- normal transport ---
        Row {
            id: content
            visible: !root.resumeOnly
            anchors.centerIn: parent
            spacing: 10

            Pill {
                label: "Exit Guided"
                onActivated: root.exitRequested()
            }

            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // Panel Step — manual one-step back / forward through the serialized path.
            Pill {
                label: "‹"
                onActivated: if (root.controller) root.controller.retreat()
            }
            Pill {
                label: "Panel Step  ›"
                onActivated: if (root.controller) root.controller.advance()
            }

            // Auto Read — play / pause the same path.
            Pill {
                label: root._auto ? "⏸  Pause" : "▶  Auto Read"
                primary: !root._auto
                onActivated: {
                    if (!root.controller) return
                    if (root._auto) root.controller.pauseAutoRead()
                    else root.controller.startAutoRead()
                }
            }

            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // Reading speed (0.5×–2.0×, quarter steps) — scales both hold and glide.
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Pill {
                    label: "−"
                    onActivated: if (root.controller) root.controller.speed = root._clampSpeed(root._speed - 0.25)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    horizontalAlignment: Text.AlignHCenter
                    text: root._speed.toFixed(2).replace(/0$/, "") + "×"
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                Pill {
                    label: "+"
                    onActivated: if (root.controller) root.controller.speed = root._clampSpeed(root._speed + 0.25)
                }
            }

            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // Live analysis status.
            GuidedAnalysisDetails {
                anchors.verticalCenter: parent.verticalCenter
                summary: root.analysis
            }
        }
    }
}

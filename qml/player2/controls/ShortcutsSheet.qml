import QtQuick
import "Player2Shortcuts.js" as Shortcuts

// A neutral, dismissible "keyboard shortcuts" reference overlay — a faithful re-implementation of the
// current player's ShortcutsSheet (Feature 7): same centred panel, neutral group headers (no colour
// accents), label-left / keys-right rows, and "Esc to close" hint. Fed by Player2Shortcuts.groups(),
// the guarded single source of truth for Player 2's real bindings. Plain QtQuick only (no Controls).
Item {
    id: sheet

    property QtObject theme
    property bool open: false

    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.96)
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"
    readonly property color edge: Qt.rgba(1, 1, 1, 0.10)

    anchors.fill: parent
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    // Dim backdrop; a click anywhere outside the panel dismisses the sheet.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: sheet.open = false }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(560, sheet.width - 80)
        implicitHeight: header.height + 1 + groupsCol.implicitHeight + 24
        height: Math.min(implicitHeight, sheet.height - 120)
        radius: 14
        color: sheet.panelColor
        border.width: 1
        border.color: sheet.edge

        // Swallow clicks on the panel body so they don't reach the backdrop and dismiss it.
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }

        Item {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 52
            Text {
                anchors.left: parent.left; anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "Keyboard shortcuts"
                color: sheet.ink
                font.family: "Segoe UI"; font.pixelSize: 15; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "Esc to close"
                color: sheet.inkDimmer
                font.family: "Segoe UI"; font.pixelSize: 11
            }
        }

        Rectangle {
            anchors.top: header.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 1; color: sheet.edge
        }

        Column {
            id: groupsCol
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 14

            Repeater {
                model: Shortcuts.groups()
                Column {
                    required property var modelData
                    width: groupsCol.width
                    spacing: 6

                    Text {
                        text: (modelData.group || "").toUpperCase()
                        color: sheet.inkDimmer
                        font.family: "Segoe UI"; font.pixelSize: 10; font.letterSpacing: 1.5
                    }

                    Repeater {
                        model: modelData.items
                        Item {
                            required property var modelData
                            width: parent.width
                            height: Math.max(24, labelText.implicitHeight)

                            Text {
                                id: labelText
                                anchors.left: parent.left
                                anchors.right: keyRow.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.note && modelData.note.length
                                      ? (modelData.label + "  —  " + modelData.note)
                                      : modelData.label
                                color: sheet.ink
                                font.family: "Segoe UI"; font.pixelSize: 13
                                elide: Text.ElideRight; wrapMode: Text.NoWrap
                            }

                            Row {
                                id: keyRow
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 5
                                Repeater {
                                    model: modelData.keys
                                    Rectangle {
                                        required property var modelData
                                        height: 20
                                        width: Math.max(20, keyText.implicitWidth + 12)
                                        radius: 4
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        border.width: 1; border.color: sheet.edge
                                        Text {
                                            id: keyText
                                            anchors.centerIn: parent
                                            text: modelData
                                            color: sheet.ink
                                            font.family: "Segoe UI"; font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ShortcutsSheet — a neutral, dismissible "keyboard shortcuts" reference overlay (Feature 7).
// Fed by PlayerHotkeys.groups(). No color accents beyond the existing theme, no emoji. This is a
// read-only cheat-sheet, NOT a layout/profile editor.
import QtQuick

Item {
    id: sheet
    anchors.fill: parent

    property bool open: false
    property var groups: []
    signal dismissed()

    visible: open
    z: 60

    Theme { id: theme }

    // Dim backdrop; a click anywhere outside the panel dismisses the sheet.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: sheet.dismissed()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 80)
        height: Math.min(header.height + 24 + flick.contentHeight + 24, parent.height - 120)
        radius: 14
        color: Qt.rgba(0.04, 0.05, 0.07, 0.96)
        border.width: 1
        border.color: theme.edge

        // Swallow clicks on the panel body so they don't reach the backdrop and dismiss it.
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }

        Item {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 52
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "Keyboard shortcuts"
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "Esc to close"
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 11
            }
        }

        Rectangle {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: theme.edge
        }

        Flickable {
            id: flick
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            clip: true
            contentHeight: groupsCol.height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: groupsCol
                width: flick.width
                spacing: 14

                Repeater {
                    model: sheet.groups
                    Column {
                        width: groupsCol.width
                        spacing: 6

                        Text {
                            text: (modelData.group || "").toUpperCase()
                            color: theme.inkDimmer
                            font.family: theme.hud
                            font.pixelSize: 10
                            font.letterSpacing: 1.5
                        }

                        Repeater {
                            model: modelData.items
                            Item {
                                width: groupsCol.width
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
                                    color: theme.ink
                                    font.family: theme.hud
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }

                                Row {
                                    id: keyRow
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5

                                    Repeater {
                                        model: modelData.keys
                                        Rectangle {
                                            height: 20
                                            width: Math.max(20, keyText.implicitWidth + 12)
                                            radius: 4
                                            color: Qt.rgba(1, 1, 1, 0.06)
                                            border.width: 1
                                            border.color: theme.edge
                                            Text {
                                                id: keyText
                                                anchors.centerIn: parent
                                                text: modelData
                                                color: theme.ink
                                                font.family: theme.hud
                                                font.pixelSize: 11
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
}

// Taskbar.qml - the OS-shell's auto-hiding switcher bar.
// The closed Colosseum button and the open taskbar are the same object, so the bar
// grows out of the button instead of swapping between two separate pieces.
import QtQuick
import QtQuick.Layouts

Item {
    id: bar
    anchors.fill: parent

    property var groups: (Sessions.revision, Sessions.groups())
    property string activeId: Sessions.activeId
    property bool open: false
    readonly property int leftEdge: Math.max(18, Math.min(80, parent.width * 0.045))
    readonly property int bottomGap: 16
    readonly property int closedSize: 64

    signal switchRequested(string id)
    signal closeRequested(string id)
    signal startClicked()

    onOpenChanged: if (!open) fan.visible = false

    function groupHasActive(group) {
        var sessions = group.sessions || []
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === bar.activeId) return true
        }
        return false
    }

    Rectangle {
        id: dock
        x: bar.leftEdge
        y: parent.height - height - bar.bottomGap
        width: bar.open ? Math.min(parent.width - (bar.leftEdge * 2), 1720) : bar.closedSize
        height: bar.closedSize
        radius: bar.open ? 18 : bar.closedSize / 2
        clip: true
        color: startMa.containsMouse || bar.open ? Qt.rgba(0.02, 0.02, 0.04, 0.78)
                                                 : Qt.rgba(0.02, 0.02, 0.03, 0.72)
        border.width: 1
        border.color: startMa.containsMouse ? Qt.rgba(0.94, 0.76, 0.35, 0.56)
                                            : Qt.rgba(1, 1, 1, 0.16)

        Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 14
            spacing: 14

            Item {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.fill: parent
                    radius: bar.open ? 14 : 24
                    color: startMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                    border.width: bar.open ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.13)

                    Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                }

                Image {
                    anchors.centerIn: parent
                    width: 28; height: 28
                    source: "../assets/icons/colosseum.svg"
                    fillMode: Image.PreserveAspectFit
                }

                MouseArea {
                    id: startMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.open = !bar.open
                }
            }

            Row {
                Layout.fillWidth: true
                spacing: 10
                opacity: bar.open ? 1 : 0
                enabled: bar.open

                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Repeater {
                    model: bar.groups
                    delegate: Rectangle {
                        id: tile
                        required property var modelData

                        property bool isActive: bar.groupHasActive(modelData)
                        property bool multi: (modelData.sessions || []).length > 1

                        width: tileRow.implicitWidth + 26
                        height: 46
                        radius: 13
                        color: tileMa.containsMouse || tile.isActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                        border.width: tile.isActive ? 1 : 0
                        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.85)

                        Row {
                            id: tileRow
                            anchors.centerIn: parent
                            spacing: 9

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20; height: 20
                                source: modelData.icon
                                fillMode: Image.PreserveAspectFit
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title + (tile.multi ? "  (" + modelData.sessions.length + ")" : "")
                                color: "#f1f1f4"
                                font.pixelSize: 13
                            }
                        }

                        MouseArea {
                            id: tileMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var sessions = tile.modelData.sessions || []
                                if (sessions.length === 1) {
                                    bar.switchRequested(sessions[0].id)
                                    bar.open = false
                                } else {
                                    fan.openFor(tile, sessions)
                                }
                            }
                        }
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 12
                opacity: bar.open ? 0.78 : 0
                enabled: bar.open

                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Repeater {
                    model: ["wifi", "bluetooth", "battery"]
                    delegate: Image {
                        required property string modelData
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18
                        source: "../assets/icons/" + modelData + ".svg"
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }
        }
    }

    Rectangle {
        id: fan
        property var sessions: []
        width: 292
        visible: false
        height: fanCol.implicitHeight + 16
        radius: 18
        color: Qt.rgba(0.04, 0.04, 0.06, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)

        function openFor(tile, nextSessions) {
            fan.sessions = nextSessions
            var point = tile.mapToItem(bar, 0, 0)
            fan.x = Math.min(Math.max(bar.leftEdge, point.x), bar.width - fan.width - bar.leftEdge)
            fan.y = dock.y - fan.height - 8
            fan.visible = true
        }

        Column {
            id: fanCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Repeater {
                model: fan.sessions
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 40
                    radius: 10
                    color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 60
                        elide: Text.ElideRight
                        text: modelData.title
                        color: "#eaeaef"
                        font.pixelSize: 13
                    }

                    Item {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: 45
                        }

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: -45
                        }

                        MouseArea {
                            id: closeMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                bar.closeRequested(modelData.id)
                                fan.visible = false
                            }
                        }
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            bar.switchRequested(modelData.id)
                            fan.visible = false
                            bar.open = false
                        }
                    }
                }
            }
        }
    }
}

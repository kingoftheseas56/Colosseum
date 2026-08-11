// NextToOpenTray â€” the temporary, explicit queue for a multi-file local-media open (Slice 20).
// The host owns the in-memory model and the actual route; this component only paints rows and
// emits the user's open/remove gesture. It never advances itself and has no persistence seam.
import QtQuick

Rectangle {
    id: tray
    objectName: "nextToOpenTray"

    property var model: []
    readonly property int stagedCount: model ? model.length : 0
    signal openRequested(int index, var entry)
    signal removeRequested(int index, var entry)

    width: 382
    height: Math.min(360, content.implicitHeight + 18)
    radius: 14
    color: Qt.rgba(0.045, 0.048, 0.065, 0.98)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.18)

    Column {
        id: content
        x: 9; y: 9
        width: parent.width - 18
        spacing: 4

        Item {
            width: parent.width
            height: 30
            Text {
                anchors.left: parent.left; anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                text: "Next to Open"
                color: "#ededf1"; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                text: tray.stagedCount
                color: "#b9b9c2"; font.pixelSize: 12
            }
        }

        Repeater {
            model: tray.model
            delegate: Rectangle {
                objectName: "nextToOpenRow_" + index
                width: content.width
                height: 54
                radius: 9
                color: rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.11)
                                               : Qt.rgba(1, 1, 1, 0.045)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)

                Column {
                    anchors.left: parent.left; anchors.leftMargin: 11
                    anchors.right: removeButton.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: modelData.title || modelData.path || "Untitled"
                        color: "#ededf1"; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.accepted ? (modelData.family || "Local") :
                                                    "Unavailable â€” " + (modelData.reject || "cannot open")
                        color: modelData.accepted ? "#92929c" : "#c1a8a8"
                        font.pixelSize: 10; elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tray.openRequested(index, modelData)
                }
                Rectangle {
                    id: removeButton
                    objectName: "nextToOpenRemove_" + index
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 27; height: 27; radius: 8
                    color: removeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.16)
                                                       : Qt.rgba(1, 1, 1, 0.06)
                    Text { anchors.centerIn: parent; text: "×"; color: "#d6d6dc"; font.pixelSize: 16 }
                    MouseArea {
                        id: removeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tray.removeRequested(index, modelData)
                    }
                }
            }
        }
    }
}

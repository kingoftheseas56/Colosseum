// The second-stage picker: when a chosen torrent's manifest is ambiguous (a
// multi-volume or otherwise unclear pack), the user picks exactly one eligible
// comic archive here. Only backend-validated CBR/CBZ/CB7/CBT candidates appear;
// each path + size is labelled so a split or wrong-volume release is obvious.
import QtQuick
import QtQuick.Controls

Item {
    id: picker

    property var files: []
    signal archiveChosen(int fileIndex)

    Theme { id: t }

    Item {
        id: head
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52
        Text {
            anchors.left: parent.left; anchors.leftMargin: 26
            anchors.verticalCenter: parent.verticalCenter
            text: picker.files.length + (picker.files.length === 1 ? " comic archive" : " comic archives")
            color: t.ink; font.family: t.display; font.pixelSize: 16; font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 26
            anchors.verticalCenter: parent.verticalCenter
            text: "CHOOSE ONE"; color: t.gold; font.family: t.ui
            font.pixelSize: 12; font.letterSpacing: 3
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: t.edge }
    }

    ListView {
        id: fileList
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: head.bottom; anchors.bottom: parent.bottom
        anchors.topMargin: 4; anchors.bottomMargin: 8
        clip: true
        model: picker.files
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: fileList }

        delegate: Item {
            id: fileRow
            required property var modelData
            width: ListView.view.width
            height: 92

            Rectangle {
                anchors.fill: parent
                color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
            }

            Rectangle {
                id: extBadge
                anchors.left: parent.left; anchors.leftMargin: 26
                anchors.verticalCenter: parent.verticalCenter
                width: 54; height: 54; radius: 12
                color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: t.edge
                Text {
                    anchors.centerIn: parent
                    text: String(fileRow.modelData.extension || "").toUpperCase()
                    color: t.inkDim; font.family: t.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
            }
            Column {
                anchors.left: extBadge.right; anchors.leftMargin: 24
                anchors.right: pick.left; anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    width: parent.width
                    text: String(fileRow.modelData.name || "")
                    color: t.ink; font.family: t.ui; font.pixelSize: 15; elide: Text.ElideMiddle
                }
                Text {
                    text: String(fileRow.modelData.sizeText || "")
                    color: t.inkDim; font.family: t.ui; font.pixelSize: 13
                }
            }
            Rectangle {
                id: pick
                anchors.right: parent.right; anchors.rightMargin: 30
                anchors.verticalCenter: parent.verticalCenter
                width: 56; height: 56; radius: 28; color: t.gold
                scale: rowMa.containsMouse ? 1.05 : 1.0
                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                Text {
                    anchors.centerIn: parent; text: "↓"; color: "#1a1306"
                    font.pixelSize: 18; font.weight: Font.DemiBold
                }
            }
            MouseArea {
                id: rowMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: picker.archiveChosen(fileRow.modelData.index)
            }
        }
    }
    ScrollGlide { flick: fileList }
}

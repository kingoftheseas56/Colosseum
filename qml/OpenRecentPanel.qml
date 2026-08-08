import QtQuick

// OpenRecentPanel — the Open Media control's recent-files popup (execution plan Slice 9).
// Model-driven and self-contained so a Qt Quick Test can seed it directly. The host (Main.qml)
// supplies `model` (from LocalLaunch.recentItems()), positions it above the taskbar dock, and
// handles reopenRequested / clearRequested. Each row shows a cleaned title + a "Local" marker;
// a dead file (available == false) is dimmed and offers nothing. No color — grays/white only.
Rectangle {
    id: panel

    property var model: []
    readonly property int rowCount: model ? model.length : 0
    signal reopenRequested(var entry)
    signal clearRequested()

    width: 320
    height: contentCol.implicitHeight + 12
    radius: 14
    color: Qt.rgba(0.05, 0.05, 0.07, 0.97)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.16)

    Column {
        id: contentCol
        width: parent.width - 12
        x: 6
        y: 6
        spacing: 2

        Item {
            width: parent.width
            height: 30
            Text {
                anchors.left: parent.left; anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "Recent"; color: "#e9e9ec"; font.pixelSize: 13; font.weight: Font.DemiBold
            }
            Text {
                objectName: "openRecentClear"
                visible: panel.rowCount > 0
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "Clear"; color: clearMa.containsMouse ? "#ffffff" : "#9a9aa4"
                font.pixelSize: 12; font.underline: true
                MouseArea {
                    id: clearMa
                    anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: panel.clearRequested()
                }
            }
        }

        Text {
            visible: panel.rowCount === 0
            width: parent.width; height: 40
            text: "No recent files yet"; color: "#7a7a84"; font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }

        Repeater {
            model: panel.model
            delegate: Rectangle {
                objectName: "openRecentRow_" + index
                width: contentCol.width
                height: 42
                radius: 8
                color: rowMa.containsMouse && modelData.available ? Qt.rgba(1, 1, 1, 0.10)
                                                                  : Qt.rgba(1, 1, 1, 0.0)
                opacity: modelData.available ? 1 : 0.5
                Row {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    spacing: 8
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16; height: 16
                        source: modelData.kind === "book" ? "../assets/icons/book-library.svg"
                              : modelData.kind === "video" ? "../assets/icons/play.svg"
                              : "../assets/icons/comic-book.svg"
                        fillMode: Image.PreserveAspectFit
                        opacity: 0.8
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 16 - 8
                        Text {
                            width: parent.width
                            text: modelData.title && modelData.title.length ? modelData.title : "(untitled)"
                            color: "#e9e9ec"; font.pixelSize: 13; elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.available ? "Local" : "Unavailable"
                            color: "#8a8a92"; font.pixelSize: 10
                        }
                    }
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: modelData.available
                    cursorShape: modelData.available ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: panel.reopenRequested(modelData)
                }
            }
        }
    }
}

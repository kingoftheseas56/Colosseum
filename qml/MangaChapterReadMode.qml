import QtQuick

Rectangle {
    id: control
    objectName: "mangaChapterReadMode"

    property bool readLive: false
    signal modeRequested(bool live)

    implicitWidth: segments.implicitWidth + 8
    implicitHeight: 44
    width: implicitWidth
    height: implicitHeight
    radius: height / 2
    color: "#171920"
    border.width: 1
    border.color: theme.edge

    Theme { id: theme }

    Row {
        id: segments
        anchors.centerIn: parent
        spacing: 4

        Repeater {
            model: [
                { live: true, title: qsTr("Read Live"), glyph: "▶", name: "mangaChapterReadLiveMode" },
                { live: false, title: qsTr("Download"), glyph: "↓", name: "mangaChapterDownloadMode" }
            ]

            Rectangle {
                id: segment
                required property var modelData
                readonly property bool selected: control.readLive === modelData.live

                width: Math.max(112, caption.implicitWidth + 28)
                height: control.height - 8
                radius: height / 2
                color: selected ? theme.gold
                    : input.interactionActive ? Qt.rgba(1, 1, 1, 0.09) : "transparent"

                Behavior on color { ColorAnimation { duration: 120 } }

                Row {
                    id: caption
                    anchors.centerIn: parent
                    spacing: 7
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: segment.modelData.glyph
                        color: segment.selected ? "#241703" : theme.gold
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: segment.modelData.title
                        color: segment.selected ? "#241703" : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                }

                KeyboardAction {
                    id: input
                    objectName: segment.modelData.name
                    anchors.fill: parent
                    accessibleName: segment.modelData.title
                    accessibleDescription: segment.modelData.live
                        ? qsTr("Open a chapter as its pages arrive")
                        : qsTr("Download the chapter before opening it")
                    focusRadius: segment.radius
                    focusPrimary: segment.selected
                    spaceActivates: true
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: segment.selected
                    onTriggered: control.modeRequested(segment.modelData.live)
                }
            }
        }
    }
}

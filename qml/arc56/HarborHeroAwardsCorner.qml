pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: root
    property var award: ({})
    implicitWidth: 300
    implicitHeight: 86
    visible: String(award.headline || "").length > 0

    HarborTheme { id: theme }

    Row {
        anchors.fill: parent
        spacing: 12

        Item {
            width: 68
            height: 68
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text: "‹   ›"
                color: "#D4AF37"
                font.family: theme.display
                font.pixelSize: 48
                font.weight: Font.Light
            }

            Rectangle {
                anchors.centerIn: parent
                width: 22
                height: 30
                radius: 4
                color: "transparent"
                border.width: 2
                border.color: "#D4AF37"

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -7
                    width: 18
                    height: 3
                    radius: 1.5
                    color: "#D4AF37"
                }
            }
        }

        Column {
            width: root.width - 80
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Text {
                width: parent.width
                text: root.award.headline || ""
                color: Qt.rgba(0.97, 0.97, 0.96, 0.55)
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.8
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }

            Repeater {
                model: root.award.lines || []
                delegate: Text {
                    required property var modelData
                    width: parent.width
                    text: String(modelData)
                    color: Qt.rgba(0.97, 0.97, 0.96, 0.72)
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }
        }
    }
}

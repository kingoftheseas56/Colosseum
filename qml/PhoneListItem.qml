import QtQuick
import QtQuick.Controls.Basic
import "TouchMetrics.js" as TouchMetrics

ItemDelegate {
    id: row
    property string description: ""
    property string trailingText: ""
    implicitWidth: 280
    implicitHeight: Math.max(TouchMetrics.minimumTargetSize, labels.implicitHeight + topPadding + bottomPadding)
    padding: 12
    Theme { id: theme }
    Accessible.name: text
    background: Rectangle { radius: 10; color: row.down || row.highlighted ? "#35343e" : "transparent" }
    contentItem: Item {
        implicitHeight: labels.implicitHeight
        Column {
            id: labels
            width: parent.width - (trailing.visible ? trailing.width + 12 : 0)
            spacing: 4
            Text { width: parent.width; text: row.text; wrapMode: Text.Wrap; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(15) }
            Text { width: parent.width; visible: text.length > 0; text: row.description; wrapMode: Text.Wrap; color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(12) }
        }
        Text {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: text.length > 0
            text: row.trailingText
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
        }
    }
}

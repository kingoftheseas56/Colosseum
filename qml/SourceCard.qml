import QtQuick
import QtQuick.Controls.Basic
import "TouchMetrics.js" as TouchMetrics

Item {
    id: card
    property string provider: ""
    property string quality: ""
    property string title: ""
    property string details: ""
    property var badges: []
    property string warning: ""
    property string status: ""
    property real progress: -1
    property string actionText: qsTr("Choose")
    property bool actionEnabled: true
    property var secondaryActions: []
    property string contentObjectName: ""
    property string actionObjectName: ""
    property string warningObjectName: ""
    signal triggered()
    signal secondaryTriggered(string action)
    implicitHeight: Math.max(TouchMetrics.minimumTargetSize, content.implicitHeight + 28)
    height: implicitHeight
    Theme { id: theme }
    Rectangle { anchors.fill:parent; color: primary.hovered ? "#24232c" : "#17171f" }
    MouseArea { anchors.fill:parent; enabled:card.actionEnabled; onClicked:card.triggered() }
    Column {
        id: content
        x: 14
        y: 14
        width: Math.max(0, card.width - 28)
        spacing: 8
        Column {
        id: metadata
        objectName: card.contentObjectName
        width: parent.width
        spacing: 8
        Text { width:parent.width; visible:text.length>0; text:card.provider; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.ink; font.family:theme.ui; font.pixelSize:theme.fontSize(15); font.weight:Font.DemiBold }
        Text { width:parent.width; visible:text.length>0; text:card.quality; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.gold; font.family:theme.ui; font.pixelSize:theme.fontSize(13); font.weight:Font.DemiBold }
        Text { width:parent.width; visible:text.length>0; text:card.title; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.ink; font.family:theme.ui; font.pixelSize:theme.fontSize(14) }
        Text { width:parent.width; visible:text.length>0; text:card.details; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.inkDimmer; font.family:theme.ui; font.pixelSize:theme.fontSize(12) }
        Flow {
            width:parent.width
            spacing:7
            visible:card.badges.length>0
            Repeater {
                model:card.badges
                delegate:Rectangle {
                    required property var modelData
                    width:Math.min(content.width, label.implicitWidth + 16)
                    height:label.implicitHeight + 10
                    radius:6
                    color:"#25242d"
                    Text { id:label; x:8; y:5; width:parent.width-16; text:String(parent.modelData); wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.inkDim; font.family:theme.ui; font.pixelSize:theme.fontSize(10) }
                }
            }
        }
        Text { objectName:card.warningObjectName; width:parent.width; visible:text.length>0; text:card.warning; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.gold; font.family:theme.ui; font.pixelSize:theme.fontSize(12) }
        Text { width:parent.width; visible:text.length>0; text:card.status; wrapMode:Text.WrapAtWordBoundaryOrAnywhere; color:theme.gold; font.family:theme.ui; font.pixelSize:theme.fontSize(12) }
        ProgressBar { width:parent.width; visible:card.progress>=0; from:0; to:1; value:card.progress }
        }
        Flow {
            width:parent.width
            spacing:8
            TouchButton {
                id:primary
                objectName:card.actionObjectName
                width:Math.min(parent.width, Math.max(80, implicitWidth))
                height:TouchMetrics.minimumTargetSize
                text:card.actionText
                enabled:card.actionEnabled
                focusPolicy:Qt.NoFocus
                onClicked:card.triggered()
                background:Rectangle { radius:12; color:primary.down ? "#dfb22d" : theme.gold; opacity:primary.enabled ? 1 : 0.5 }
                contentItem:Text { text:primary.text; color:"#1a1306"; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter; elide:Text.ElideRight; font.family:theme.ui; font.pixelSize:theme.fontSize(14); font.weight:Font.DemiBold }
            }
            Repeater {
                model:card.secondaryActions
                delegate:TouchButton {
                    required property var modelData
                    objectName:modelData.objectName || ""
                    text:modelData.label
                    width:Math.min(content.width, Math.max(TouchMetrics.minimumTargetSize, implicitWidth))
                    height:TouchMetrics.minimumTargetSize
                    enabled:modelData.enabled !== false
                    focusPolicy:Qt.NoFocus
                    onClicked:card.secondaryTriggered(modelData.id)
                }
            }
        }
    }
}

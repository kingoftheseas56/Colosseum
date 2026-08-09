import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Rectangle {
    id: root
    objectName: "guideContextStrip"
    color: "#181818"
    border.color: "#3b3b3b"
    border.width: 1
    radius: 2
    implicitHeight: visible ? content.implicitHeight + 28 : 0

    property string originLabel: ""
    property string summary: ""
    property bool recognized: false
    signal relevantHelpRequested()
    signal returnRequested()

    visible: recognized && originLabel.length > 0

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 14
        spacing: 5

        Text {
            text: "FROM WHERE YOU WERE"
            color: "#9b9b9b"
            font.pixelSize: 11
            font.letterSpacing: 1.1
        }
        Text {
            text: root.originLabel
            color: "#f2f2f2"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            width: parent.width
        }
        Text {
            text: root.summary
            visible: text.length > 0
            color: "#c4c4c4"
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: 13
        }
        Row {
            spacing: 10
            Basic.Button {
                text: "Show relevant help"
                visible: root.summary.length > 0
                activeFocusOnTab: true
                onClicked: root.relevantHelpRequested()
            }
            Basic.Button {
                objectName: "guideReturnAction"
                text: "Return to " + root.originLabel
                activeFocusOnTab: true
                onClicked: root.returnRequested()
            }
        }
    }
}

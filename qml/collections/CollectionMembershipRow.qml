import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Button {
    id: root
    required property var collection
    required property var store
    property bool included: false
    Theme { id: theme }
    implicitHeight: 70
    objectName: "membership_" + collection.id
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: collection.name
    Accessible.checkable: true
    Accessible.checked: included
    Accessible.description: qsTr("%1 titles").arg(collection.items.length)
    leftPadding: 10
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    background: Rectangle {
        radius: 10
        color: root.included ? "#15f0c44a" : root.hovered ? "#12ffffff" : "transparent"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? theme.gold : root.included ? "#40f0c44a" : "transparent"
    }
    contentItem: RowLayout {
        spacing: 14
        Rectangle {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 52
            radius: 5
            color: "#1affffff"
            CollectionIcon { anchors.centerIn: parent; name: "collections"; visible: root.collection.items.length === 0 }
            CollectionCover {
                anchors.fill: parent
                source: root.collection.items.length ? root.store.media(root.collection.items[0]).cover : ""
                visible: root.collection.items.length > 0
                title: root.collection.name
            }
        }
        ColumnLayout {
            spacing: 3
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.collection.name
                textFormat: Text.PlainText
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: theme.ink
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("%1 titles").arg(root.collection.items.length)
                font.family: theme.ui
                font.pixelSize: 12
                color: theme.inkDimmer
            }
        }
        Rectangle {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            radius: 6
            color: root.included ? theme.gold : "transparent"
            border.color: root.included ? theme.gold : "#66ffffff"
            CollectionIcon { anchors.centerIn: parent; width: 16; height: 16; name: "check"; strokeColor: "#15130c"; visible: root.included }
        }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Select) {
            if (root.enabled) root.clicked()
            event.accepted = true
        }
    }

}

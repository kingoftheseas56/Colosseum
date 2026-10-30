import QtQuick
import QtQuick.Controls.Basic
import ".."

TextField {
    id: root
    Theme { id: theme }
    property alias clearControl: clearButton
    implicitHeight: 44
    implicitWidth: 260
    color: theme.ink
    placeholderTextColor: theme.inkDimmer
    font.family: theme.ui
    font.pixelSize: 14
    selectionColor: "#66f0c44a"
    selectedTextColor: theme.ink
    leftPadding: 40
    rightPadding: 40
    topPadding: 10
    bottomPadding: 10
    selectByMouse: true
    Accessible.name: placeholderText
    background: Rectangle {
        radius: 10
        color: "#0bffffff"
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? theme.gold : theme.edge
    }
    CollectionIcon {
        name: "search"
        strokeColor: theme.inkDimmer
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 17
        height: 17
    }
    CollectionActionButton {
        id: clearButton
        objectName: root.objectName + "Clear"
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 34
        compact: true
        emphasis: "ghost"
        iconName: "close"
        visible: root.text.length > 0
        Accessible.name: qsTr("Clear search")
        onClicked: { root.clear(); root.forceActiveFocus(Qt.OtherFocusReason) }
    }
}

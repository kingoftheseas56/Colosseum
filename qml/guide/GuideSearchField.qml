import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Basic.TextField {
    id: root
    objectName: "guideSearch"
    placeholderText: "What do you want to do?"
    activeFocusOnTab: true
    Accessible.name: placeholderText

    property bool focusVisible: activeFocus
    signal queryChanged(string query)

    leftPadding: 16
    rightPadding: 16
    implicitHeight: 46
    color: "#f4f4f4"
    placeholderTextColor: "#969696"
    font.pixelSize: 15
    selectByMouse: true
    background: Rectangle {
        color: "#171717"
        border.color: root.activeFocus ? "#f0f0f0" : "#4d4d4d"
        border.width: root.activeFocus ? 2 : 1
        radius: 2
    }
    onTextChanged: queryChanged(text)
}

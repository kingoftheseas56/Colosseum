import QtQuick

Rectangle {
    property bool selected: false
    width: 28; height: 28; radius: 14
    color: selected ? "#f0c44a" : "#181821"
    border.width: 2; border.color: "#f0c44a"
    Accessible.ignored: true
    Text {
        anchors.centerIn: parent
        text: parent.selected ? "✓" : ""
        color: "#16120b"; font.pixelSize: 18; font.bold: true
        Accessible.ignored: true
    }
}

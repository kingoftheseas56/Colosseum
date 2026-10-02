import QtQuick

Rectangle {
    id: root
    required property var controller
    property real unit: 16
    property string name: ""
    property string detail: ""
    property string glyph: ""
    property bool selected: false
    property bool focusedState: false
    signal triggered()
    signal entered()

    height: 4.1 * unit
    radius: 0.85 * unit
    color: focusedState ? Qt.rgba(240/255,196/255,74/255,0.11)
                        : selected ? Qt.rgba(240/255,196/255,74/255,0.075) : "transparent"
    border.width: 1
    border.color: focusedState ? controller.gold
                               : selected ? Qt.rgba(240/255,196/255,74/255,0.24) : "transparent"

    Rectangle {
        visible: root.selected
        x: -0.6 * unit
        y: unit
        width: 0.2 * unit
        height: 2 * unit
        radius: width / 2
        color: controller.gold
    }
    Rectangle {
        x: 0.7 * unit
        anchors.verticalCenter: parent.verticalCenter
        width: 2.8 * unit
        height: 2.8 * unit
        radius: 0.8 * unit
        color: Qt.rgba(1,1,1,0.05)
        border.width: 1
        border.color: Qt.rgba(1,1,1,0.1)
        PorticoCombinedGlyph {
            anchors.centerIn: parent
            width: 1.5 * unit
            height: 1.5 * unit
            glyphKey: root.glyph
            tone: controller.ink
        }
    }
    Column {
        x: 4.35 * unit
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 5.3 * unit
        spacing: 0.1 * unit
        Text {
            width: parent.width
            text: root.name
            color: controller.ink
            font.family: controller.uiFont
            font.pixelSize: 0.98 * unit
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: root.detail
            color: controller.slate
            font.family: controller.uiFont
            font.pixelSize: 0.8 * unit
            elide: Text.ElideRight
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: root.entered()
        onClicked: {
            root.entered()
            root.triggered()
        }
    }
}

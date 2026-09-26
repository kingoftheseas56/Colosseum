import QtQuick
import ".." as Colosseum

Item {
    id: root
    property real unit: 16
    property string providerKey: ""
    property string title: ""
    property string numberText: ""
    property bool selected: false
    property bool moving: false
    property bool addMode: false
    property color ink: "#f7f7f5"
    property color mist: "#c9c8d0"
    property color slate: "#9a99a5"
    property color gold: "#f0c44a"
    property string displayFont: "Fraunces"
    signal triggered()
    signal entered()

    scale: moving ? 1.14 : (selected ? 1.08 : 1.0)
    z: moving ? 6 : (selected ? 4 : 1)
    transformOrigin: Item.Center
    Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: 1.2 * root.unit
        color: root.addMode ? "transparent"
                            : (root.selected ? Qt.rgba(1,1,1,0.13) : Qt.rgba(1,1,1,0.045))
        border.width: 1
        border.color: root.addMode ? Qt.rgba(1,1,1,0.18) : Qt.rgba(1,1,1,0.10)
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: -0.1875 * root.unit
        radius: 1.38 * root.unit
        color: "transparent"
        border.width: 0.1875 * root.unit
        border.color: root.selected ? root.gold : "transparent"
        visible: root.selected
    }
    Rectangle {
        anchors.fill: parent
        radius: 1.2 * root.unit
        visible: root.selected && !root.addMode
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(1,1,1,0.10) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: 0.7
    }
    Text {
        visible: root.numberText.length > 0
        x: 0.8 * root.unit
        y: 0.65 * root.unit
        text: root.numberText
        color: root.slate
        opacity: 0.9
        font.family: "Segoe UI"
        font.pixelSize: 0.72 * root.unit
    }
    Column {
        anchors.centerIn: parent
        spacing: 0.55 * root.unit
        PorticoGlyph {
            width: root.addMode ? 1.9 * root.unit : 2.5 * root.unit
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            glyphKey: root.addMode ? "plus" : root.providerKey
            tone: root.selected ? root.ink : root.mist
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(1, root.width - 1.2 * root.unit)
            text: root.title
            color: root.selected ? root.ink : root.mist
            font.family: root.displayFont
            font.pixelSize: 1.28 * root.unit
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
    Colosseum.KeyboardAction {
        id: input
        anchors.fill: parent
        focusEnabled: false
        showFocusFrame: false
        accessibleName: root.title
        onTriggered: root.triggered()
    }
    Connections {
        target: input
        function onHoveredChanged() { if (input.hovered) root.entered() }
    }
}

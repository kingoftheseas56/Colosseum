import QtQuick
import QtQuick.Effects

Glass {
    id: root
    required property string label
    required property url iconSource
    property bool compact: false
    property bool highlighted: false

    implicitWidth: caption.implicitWidth + 30
    width: implicitWidth
    height: 39
    radius: 10
    blurMax: 14
    tint: 0
    scrim: 0
    edge: "transparent"

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.highlighted ? "#9914191e" : "#5112171c"
        border.color: root.highlighted ? "#66f0c44a" : "#3bffffff"
        Behavior on color { ColorAnimation { duration: 150 } }
    }
    Row {
        id: caption
        anchors.centerIn: parent
        spacing: 9
        Item {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            Image {
                id: glyph
                anchors.fill: parent
                source: root.iconSource
                sourceSize.width: 32
                sourceSize.height: 32
                visible: false
            }
            MultiEffect {
                anchors.fill: parent
                source: glyph
                colorization: 1
                colorizationColor: "#f0c44a"
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: "#e5e3df"
            font.family: "Segoe UI"
            font.pixelSize: root.compact ? 11 : 12
            font.weight: Font.Medium
        }
    }
}

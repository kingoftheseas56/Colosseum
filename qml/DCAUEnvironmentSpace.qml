import QtQuick

Item {
    id: root
    property bool reducedMotion: false
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#060914" }
            GradientStop { position: 1; color: "#010206" }
        }
    }
    Repeater {
        model: 64
        delegate: Rectangle {
            required property int index
            width: index % 11 === 0 ? 3 : 2; height: width; radius: width/2
            x: ((index * 83) % 997) / 997 * parent.width
            y: ((index * 137) % 811) / 811 * parent.height * 0.78
            color: index % 5 === 0 ? "#b9d5e8" : "#f7f7f5"
            opacity: 0.35 + (index % 7) * 0.08
        }
    }
    Rectangle {
        width: parent.width * 1.35; height: width; radius: width/2
        x: -width * 0.13; y: parent.height * 0.70
        color: "#0a2134"; border.width: 2; border.color: Qt.rgba(120/255,210/255,240/255,.32)
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; y: parent.height * 0.67; height: 110
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(70/255,160/255,210/255,.13) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
}

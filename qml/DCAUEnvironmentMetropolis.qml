import QtQuick

Item {
    id: root
    property bool reducedMotion: false
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#a8cede" }
            GradientStop { position: 0.58; color: "#dbe7e8" }
            GradientStop { position: 1.0; color: "#d6b795" }
        }
    }
    Rectangle { width: 120; height: 120; radius: 60; x: parent.width * 0.67; y: 70; color: "#f5dfad"; opacity: 0.42 }
    Repeater {
        model: [[0,.26,.11],[.08,.42,.13],[.19,.32,.09],[.29,.55,.14],[.44,.38,.10],[.53,.62,.14],[.68,.35,.10],[.77,.48,.12],[.90,.40,.11]]
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: parent.width*modelData[0]; width: parent.width*modelData[2]; height: parent.height*modelData[1]
            anchors.bottom: parent.bottom; color: index%2 ? "#cbb697" : "#dfccb0"
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; width: 8; height: parent.height*0.62; color: "#6fa0c2"; opacity: .85 }
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 32; width: 8; height: parent.height*0.45; color: "#6fa0c2"; opacity: .7; x: 18 }
        }
    }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 110; color: "#c69072"; opacity: .16 }
}

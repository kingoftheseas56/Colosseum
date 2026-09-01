import QtQuick

Item {
    id: root
    property bool reducedMotion: false
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3b0b12" }
            GradientStop { position: 0.48; color: "#19080d" }
            GradientStop { position: 1.0; color: "#050509" }
        }
    }
    Repeater {
        model: [
            [0.00,0.28,0.11],[0.08,0.44,0.13],[0.19,0.34,0.10],[0.28,0.52,0.13],
            [0.41,0.40,0.10],[0.50,0.59,0.14],[0.64,0.37,0.09],[0.72,0.49,0.12],
            [0.84,0.42,0.10],[0.91,0.56,0.12]
        ]
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: parent.width * modelData[0]
            width: parent.width * modelData[2]
            height: parent.height * modelData[1]
            anchors.bottom: parent.bottom
            color: index % 2 ? "#100c16" : "#17101a"
            Rectangle { width: parent.width * 0.18; height: parent.height * 0.72; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; color: "#24111a" }
        }
    }
    Rectangle {
        width: parent.width * 0.7; height: 18; x: parent.width * 0.18; y: parent.height * 0.24
        rotation: -14; opacity: 0.09; color: "#ff7a72"
        NumberAnimation on rotation { running: !root.reducedMotion; from: -18; to: 8; duration: 12000; loops: Animation.Infinite; easing.type: Easing.InOutSine }
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: parent.height * 0.24; opacity: 0.28
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: "#771c24" }
        }
    }
}

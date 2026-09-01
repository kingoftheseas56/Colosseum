import QtQuick

Item {
    id: root
    property bool reducedMotion: false
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#151124" }
            GradientStop { position: 0.55; color: "#35142c" }
            GradientStop { position: 1; color: "#160c18" }
        }
    }
    Rectangle { width: 76; height: 76; radius: 38; x: parent.width*.78; y: 88; color: "#b88386"; opacity: .72 }
    Repeater {
        model: [[0,.32,.11],[.08,.53,.12],[.19,.41,.10],[.29,.64,.13],[.43,.45,.09],[.52,.70,.13],[.66,.39,.09],[.74,.57,.12],[.88,.48,.12]]
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: parent.width*modelData[0]; width: parent.width*modelData[2]; height: parent.height*modelData[1]
            anchors.bottom: parent.bottom; color: index%2 ? "#171129" : "#21132d"
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height*.44; width: parent.width*.55; height: 3; color: "#d14b70"; opacity: .45 }
        }
    }
    Rectangle { x: parent.width*.34; y: parent.height*.58; width: parent.width*.42; height: 5; color: "#b04c78"; opacity: .28 }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: parent.height*.24; color: "#a52f50"; opacity: .13 }
}

pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: presentation
    objectName: "bootSplashPresentation"

    property bool booting: true
    property bool includeVinyl: false
    property bool reducedMotion: false

    readonly property bool motion: visible && booting && !reducedMotion
    readonly property int firstScene: includeVinyl ? 0 : 1
    property int sceneOffset: 0
    readonly property int sceneIndex: firstScene + sceneOffset % (4 - firstScene)
    readonly property var ambient: ["#413149", "#3d263b", "#4b3125", "#233649"]
    readonly property real stageScale: Math.min(1.08, width * 0.91 / 1080, height * 0.59 / 420)

    onMotionChanged: {
        if (!motion)
            transition.stop()
        if (reducedMotion)
            stage.opacity = 1
    }

    FontLoader { id: bodyFont; source: "../assets/fonts/selawik/Selawik-Regular.ttf" }

    Rectangle {
        anchors.fill: parent
        color: "#080a0e"
    }

    Image {
        anchors.fill: parent
        source: "../assets/wallpaper/cold-ripple.jpg"
        sourceSize: Qt.size(1400, 880)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: 0.26
        Accessible.ignored: true
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.15; color: "transparent" }
            GradientStop {
                position: 0.46
                color: presentation.ambient[presentation.sceneIndex]
                Behavior on color {
                    enabled: !presentation.reducedMotion
                    ColorAnimation { duration: 570 }
                }
            }
            GradientStop { position: 0.8; color: "transparent" }
        }
        opacity: 0.26
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#e206080b" }
            GradientStop { position: 0.33; color: "#1006080b" }
            GradientStop { position: 0.65; color: "#1806080b" }
            GradientStop { position: 1; color: "#f106080b" }
        }
        Accessible.ignored: true
    }

    Image {
        id: wordmark
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(Math.max(18, parent.height * 0.095))
        width: Math.min(255, parent.width * 0.56, parent.height * 0.48)
        height: width * 0.25
        fillMode: Image.PreserveAspectFit
        source: "../assets/icons/colosseum-wordmark.svg"
        sourceSize: Qt.size(510, 130)
        Accessible.name: qsTr("Colosseum")
    }

    Item {
        id: stage
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - parent.height * 0.014
        width: 1080
        height: 420
        scale: presentation.stageScale

        BootSplashScenes {
            anchors.fill: parent
            sceneIndex: presentation.sceneIndex
            motion: presentation.motion
            reducedMotion: presentation.reducedMotion
        }
    }

    SequentialAnimation {
        id: transition
        NumberAnimation { target: stage; property: "opacity"; to: 0; duration: 235; easing.type: Easing.InCubic }
        ScriptAction { script: presentation.sceneOffset++ }
        NumberAnimation { target: stage; property: "opacity"; to: 1; duration: 405; easing.type: Easing.OutCubic }
    }

    Timer {
        running: presentation.motion
        repeat: true
        interval: 3800
        onTriggered: transition.start()
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(Math.max(20, parent.height * 0.082))
        spacing: 15

        Rectangle {
            id: rail
            width: Math.min(162, presentation.width * 0.39)
            height: 2
            radius: 1
            color: "#53535a"
            clip: true
            anchors.horizontalCenter: parent.horizontalCenter
            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("Opening Colosseum")

            Rectangle {
                id: pulse
                width: 42
                height: rail.height
                radius: 1
                x: (rail.width - width) / 2
                color: "#efc775"
                SequentialAnimation on x {
                    running: presentation.motion
                    loops: Animation.Infinite
                    NumberAnimation { from: -pulse.width; to: rail.width; duration: 1670; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 300 }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Opening Colosseum")
            color: "#d7d5d0"
            font.family: bodyFont.name
            font.pixelSize: 13
            font.letterSpacing: 1.3
            Accessible.ignored: true
        }
    }
}

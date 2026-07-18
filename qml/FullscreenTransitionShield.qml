import QtQuick

Item {
    id: root
    z: 10000
    visible: transitioning
    enabled: transitioning

    property bool transitioning: false
    property bool awaitingFrame: false
    signal applyRequested()

    function begin() {
        if (transitioning)
            return
        transitioning = true
        awaitingFrame = false
        cover.opacity = 0
        coverIn.restart()
    }

    function reveal() {
        if (!awaitingFrame)
            return
        awaitingFrame = false
        frameFallback.stop()
        coverOut.restart()
    }

    Rectangle {
        id: cover
        anchors.fill: parent
        color: "#08090c"
        opacity: 0
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.transitioning
        hoverEnabled: true
    }

    NumberAnimation {
        id: coverIn
        target: cover
        property: "opacity"
        from: 0
        to: 1
        duration: 60
        easing.type: Easing.OutCubic
        onFinished: {
            root.awaitingFrame = true
            root.applyRequested()
            frameFallback.restart()
        }
    }

    NumberAnimation {
        id: coverOut
        target: cover
        property: "opacity"
        from: 1
        to: 0
        duration: 90
        easing.type: Easing.OutCubic
        onFinished: {
            cover.opacity = 0
            root.transitioning = false
        }
    }

    Timer {
        id: frameFallback
        interval: 250
        repeat: false
        onTriggered: root.reveal()
    }

    Connections {
        target: root.Window.window
        function onFrameSwapped() {
            root.reveal()
        }
    }
}

import QtQuick
import "controls"

// The immersive Player 2 chrome, overlaid on the video surface. It receives the C++ `session` (typed
// state + commands) and `hostServices` (app orchestration); it renders typed state and sends typed
// intent only — no demux, no pacing, no property strings. Palette and layout track the current
// player so the parity ledger can compare them side by side.
Item {
    id: shell

    property var session
    property var hostServices
    signal fullscreenRequested()

    focus: true

    // Parity palette (matches the current player's Theme: gold accent on dark glass).
    readonly property QtObject theme: QtObject {
        readonly property color gold: "#f0c44a"
        readonly property color ink: "#f7f7f5"
        readonly property color inkDim: "#c9c8d0"
        readonly property color inkDimmer: "#9a99a5"
        readonly property color panel: Qt.rgba(0.04, 0.05, 0.07, 0.94)
        readonly property color edge: Qt.rgba(1, 1, 1, 0.18)
    }

    property bool controlsShown: true
    function wakeChrome() {
        controlsShown = true
        hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: (transportBar.paused || transportBar.buffering || !shell.session) ? 4500 : 1800
        onTriggered: {
            // Never hide the chrome while there is nothing playing to watch behind it.
            if (!transportBar.paused && !transportBar.buffering)
                shell.controlsShown = false
        }
    }
    Component.onCompleted: hideTimer.start()

    // Pointer: move wakes the chrome; left-click toggles play/pause; cursor hides with the HUD.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: shell.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: shell.wakeChrome()
        onClicked: {
            if (shell.session)
                transportBar.togglePlayPause()
            shell.wakeChrome()
        }
    }

    WheelHandler {
        // Scroll over the video adjusts volume ±5% (parity with the current player).
        onWheel: function(event) {
            if (!shell.session)
                return
            var step = event.angleDelta.y > 0 ? 0.05 : -0.05
            shell.session.setMuted(false)
            shell.session.setVolume(Math.max(0, Math.min(1, shell.session.volume + step)))
            shell.wakeChrome()
        }
    }

    Keys.onPressed: function(event) {
        shell.wakeChrome()
        switch (event.key) {
        case Qt.Key_Space:
            transportBar.togglePlayPause(); event.accepted = true; break
        case Qt.Key_Left:
            if (shell.session) shell.session.seekRelative(-10); event.accepted = true; break
        case Qt.Key_Right:
            if (shell.session) shell.session.seekRelative(10); event.accepted = true; break
        case Qt.Key_M:
            if (shell.session) shell.session.setMuted(!shell.session.muted); event.accepted = true; break
        case Qt.Key_F:
            shell.fullscreenRequested(); event.accepted = true; break
        }
    }

    // All interactive chrome fades together on auto-hide.
    Item {
        id: chrome
        anchors.fill: parent
        opacity: shell.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Rectangle { // top scrim
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 112
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.60) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        Item {
            id: bottomDock
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: transportBar.implicitHeight + 20

            Rectangle { // bottom scrim
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.45) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                }
            }

            TransportBar {
                id: transportBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                session: shell.session
                theme: shell.theme
                onFullscreenRequested: shell.fullscreenRequested()
            }
        }
    }
}

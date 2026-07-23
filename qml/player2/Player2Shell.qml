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
    readonly property bool menusOpen: transportBar.anyMenuOpen || overflowMenu.open
    function wakeChrome() {
        controlsShown = true
        hideTimer.restart()
    }
    function closeAllMenus() {
        transportBar.closeMenus()
        overflowMenu.open = false
    }

    Timer {
        id: hideTimer
        interval: (transportBar.paused || transportBar.buffering || !shell.session) ? 4500 : 1800
        onTriggered: {
            // Never hide while paused/buffering or while a menu is open.
            if (!transportBar.paused && !transportBar.buffering && !shell.menusOpen)
                shell.controlsShown = false
        }
    }
    Component.onCompleted: hideTimer.start()

    // Subtitles paint on the video, below the chrome, and persist when the chrome auto-hides.
    SubtitleLayer {
        anchors.fill: parent
        session: shell.session
        theme: shell.theme
    }

    // Pointer: move wakes the chrome; left-click toggles play/pause (or dismisses a menu); right-click
    // raises the overflow menu; the cursor hides with the HUD.
    MouseArea {
        id: videoMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: shell.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: shell.wakeChrome()
        onClicked: function(mouse) {
            shell.wakeChrome()
            if (mouse.button === Qt.RightButton) {
                // Close any open track menu first so two popovers never stack, then raise overflow.
                transportBar.closeMenus()
                shell.popupOverflow(mouse.x, mouse.y)
                return
            }
            if (shell.menusOpen) { shell.closeAllMenus(); return }
            if (shell.session) transportBar.togglePlayPause()
        }
    }

    WheelHandler {
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
        case Qt.Key_Comma:
            if (shell.session) shell.session.frameStep(-1); event.accepted = true; break
        case Qt.Key_Period:
            if (shell.session) shell.session.frameStep(1); event.accepted = true; break
        case Qt.Key_M:
            if (shell.session) shell.session.setMuted(!shell.session.muted); event.accepted = true; break
        case Qt.Key_D:
            statsOverlay.open = !statsOverlay.open; event.accepted = true; break
        case Qt.Key_F:
            shell.fullscreenRequested(); event.accepted = true; break
        case Qt.Key_Escape:
            if (shell.menusOpen) { shell.closeAllMenus(); event.accepted = true }
            break
        }
    }

    // Stats overlay persists (toggled with D / overflow) independent of the chrome fade.
    StatsOverlay {
        id: statsOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 40
        anchors.topMargin: 120
        session: shell.session
        theme: shell.theme
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

    // Right-click "more controls" menu, positioned at the cursor and clamped to the window.
    function popupOverflow(px, py) {
        overflowMenu.x = Math.max(10, Math.min(width - overflowMenu.width - 10, px))
        overflowMenu.y = Math.max(10, Math.min(height - overflowMenu.implicitHeight - 10, py))
        overflowMenu.open = true
    }
    OverflowMenu {
        id: overflowMenu
        session: shell.session
        theme: shell.theme
        onToggleStatsRequested: { statsOverlay.open = !statsOverlay.open; overflowMenu.open = false }
    }
}

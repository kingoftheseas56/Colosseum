// PlayerPage - fullscreen video player. mpv (Colosseum.Player.MpvItem) under the house
// glass chrome. Owns the stream flow: hand a torrent to `Stream`, feed mpv the URL it
// returns. Controls auto-hide; Back returns to the source list underneath.
import QtQuick
import Colosseum.Player

Item {
    id: root
    anchors.fill: parent

    property Item backdrop
    property string mediaTitle: ""
    property bool starting: false
    property bool errored: false
    property string statusMsg: ""

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- entry points -------------------------------------------------------
    // Play a torrent source: register it with the stream engine, then load the URL.
    function playTorrent(infoHash, fileIdx, title) {
        root.mediaTitle = title || ""
        root.errored = false
        root.starting = true
        root.statusMsg = "Starting stream…"
        revealControls()
        Stream.play(infoHash, fileIdx)
    }
    // Play a direct URL (test / non-torrent).
    function playUrl(url, title) {
        root.mediaTitle = title || ""
        root.errored = false
        root.starting = true
        root.statusMsg = "Opening…"
        mpv.loadFile(url)
    }
    // Stop + release on close so mpv isn't left decoding behind a hidden page.
    function stop() {
        mpv.command(["stop"])
        root.starting = false
    }

    Theme { id: theme }

    // pitch-black base behind the video
    Rectangle { anchors.fill: parent; color: "#000000" }

    MpvItem {
        id: mpv
        anchors.fill: parent
        onFileLoaded: { root.starting = false; root.statusMsg = "" }
        onEndFile: function(reason) { if (reason === "error") { root.errored = true; root.statusMsg = "Playback failed." } }
    }

    // stream engine → mpv
    Connections {
        target: Stream
        function onStreamReady(url, infoHash, fileIdx) {
            root.statusMsg = "Buffering…"
            mpv.loadFile(url)
        }
        function onStreamError(message) {
            root.starting = false
            root.errored = true
            root.statusMsg = message
        }
    }

    // ---- reveal controls on movement, auto-hide after a pause ----
    property bool controlsShown: true
    function revealControls() { root.controlsShown = true; hideTimer.restart() }
    Timer { id: hideTimer; interval: 2800; onTriggered: if (!mpv.pause && !root.starting) root.controlsShown = false }

    // fullscreen surface: move → reveal, click → play/pause (sits BELOW the control bar)
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: root.revealControls()
        onClicked: { if (!root.starting && !root.errored) mpv.pause = !mpv.pause; root.revealControls() }
    }

    // ---- centered status: starting / buffering / error ----
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: root.starting || root.errored
        // simple spinning ring while starting (not on error)
        Item {
            width: 46; height: 46; anchors.horizontalCenter: parent.horizontalCenter
            visible: root.starting && !root.errored
            Rectangle {
                anchors.fill: parent; radius: width / 2
                color: "transparent"; border.width: 3; border.color: Qt.rgba(1,1,1,0.15)
            }
            Rectangle {
                width: 12; height: 12; radius: 6; color: theme.gold
                x: parent.width / 2 - 6; y: -1
                transformOrigin: Item.Center
                RotationAnimation on rotation {
                    target: spinPivot; running: root.starting; loops: Animation.Infinite
                    from: 0; to: 360; duration: 900
                }
            }
            Item { id: spinPivot; anchors.fill: parent }
            RotationAnimation on rotation { running: root.starting; loops: Animation.Infinite; from: 0; to: 360; duration: 1100 }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.statusMsg
            color: root.errored ? "#e6a3a3" : theme.ink
            font.family: theme.ui; font.pixelSize: 16
        }
    }

    // ===================== controls overlay =====================
    Item {
        anchors.fill: parent
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220 } }

        // top scrim + Back
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 120
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.7) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.0) }
            }
        }
        Item {
            x: theme.margin; y: 28; width: backRow.implicitWidth + 16; height: 36
            Row {
                id: backRow; spacing: 6; anchors.verticalCenter: parent.verticalCenter
                Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                    font.family: theme.display; font.pixelSize: 30; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                    font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                    Behavior on color { ColorAnimation { duration: 120 } } }
            }
            MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        // title, top-center
        Text {
            anchors.top: parent.top; anchors.topMargin: 34; anchors.horizontalCenter: parent.horizontalCenter
            text: root.mediaTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
            style: Text.Raised; styleColor: Qt.rgba(0,0,0,0.4)
            elide: Text.ElideRight; width: Math.min(implicitWidth, parent.width * 0.5)
        }

        // bottom scrim + transport
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 170
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.78) }
            }
        }

        Column {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.bottomMargin: 34
            spacing: 16

            // seek bar: track + gold fill + draggable scrub
            Item {
                width: parent.width; height: 16
                Rectangle {
                    id: track
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width; height: 5; radius: 2.5
                    color: Qt.rgba(1,1,1,0.22)
                    Rectangle {
                        height: parent.height; radius: parent.radius; color: theme.gold
                        width: parent.width * (mpv.duration > 0 ? Math.max(0, Math.min(1, mpv.position / mpv.duration)) : 0)
                    }
                    Rectangle {  // scrub handle
                        width: 14; height: 14; radius: 7; color: theme.gold
                        anchors.verticalCenter: parent.verticalCenter
                        x: parent.width * (mpv.duration > 0 ? Math.max(0, Math.min(1, mpv.position / mpv.duration)) : 0) - 7
                        visible: mpv.duration > 0
                    }
                }
                MouseArea {
                    anchors.fill: parent; anchors.margins: -8
                    cursorShape: Qt.PointingHandCursor
                    enabled: mpv.duration > 0
                    function seekTo(px) { mpv.position = Math.max(0, Math.min(1, px / track.width)) * mpv.duration; root.revealControls() }
                    onPressed: (m) => seekTo(m.x)
                    onPositionChanged: (m) => { if (pressed) seekTo(m.x) }
                }
            }

            // play/pause · time
            Row {
                spacing: 22
                // play / pause button
                Rectangle {
                    width: 52; height: 52; radius: 26; color: theme.gold
                    scale: ppMa.containsMouse ? 1.06 : 1.0
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    Text {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: mpv.pause ? 2 : 0
                        text: mpv.pause ? "▶" : "❚❚"
                        color: "#1a1306"; font.pixelSize: mpv.pause ? 20 : 16; font.weight: Font.Bold
                    }
                    MouseArea { id: ppMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: { mpv.pause = !mpv.pause; root.revealControls() } }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: mpv.formattedPosition + "  /  " + mpv.formattedDuration
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                }
            }
        }
    }

    // keep controls up while paused / starting
    onStartingChanged: if (starting) revealControls()
    Connections { target: mpv; function onPauseChanged() { if (mpv.pause) root.revealControls() } }
}

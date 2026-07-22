import QtQuick

// The bottom control dock: a state line, the seek row, and the transport buttons. It renders typed
// session state and sends typed session commands only. Layout and palette track the current player.
Item {
    id: root

    property var session
    property QtObject theme
    property bool showRemaining: false
    signal fullscreenRequested()

    implicitHeight: 126

    readonly property int seekStepSeconds: 10
    readonly property bool playing: session && session.state === 3 // Player2State::Playing
    readonly property bool buffering: session && session.state === 2 // Player2State::Buffering
    readonly property bool paused: session && session.state === 4 // Player2State::Paused
    readonly property real dur: session && session.duration > 0 ? session.duration : 0
    readonly property real pos: session ? session.position : 0

    function fmt(s) { return seekBar.fmtTime(s) }
    function togglePlayPause() {
        if (!session) return
        if (playing || buffering) session.pause()
        else session.play()
    }

    // ---- a circular transport button -------------------------------------------------------------
    component RoundButton: Item {
        id: rb
        property string icon: ""
        property string seconds: ""
        property bool hero: false
        property bool active: false
        property real size: 40
        property string tooltip: ""
        signal tapped()
        implicitWidth: size
        implicitHeight: size
        scale: tap.pressed ? 0.95 : (tap.containsMouse ? 1.04 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: rb.hero ? Qt.rgba(1, 1, 1, 0.92)
                 : rb.active ? Qt.rgba(1, 1, 1, 0.16)
                 : tap.containsMouse ? Qt.rgba(1, 1, 1, 0.10)
                 : "transparent"
        }
        Player2Icon {
            anchors.fill: parent
            kind: rb.icon
            ink: rb.hero ? "#101014" : (rb.active && root.theme ? root.theme.gold
                                        : (root.theme ? root.theme.ink : "#f7f7f5"))
            accessibleName: rb.tooltip
        }
        Text {
            visible: rb.seconds !== ""
            anchors.centerIn: parent
            text: rb.seconds
            color: root.theme ? root.theme.ink : "#f7f7f5"
            font.family: "Segoe UI"
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
        }
        MouseArea {
            id: tap
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.tapped()
        }
    }

    // ---- inline volume control (mute + gold slider) ----------------------------------------------
    component VolumeControl: Item {
        id: vol
        implicitWidth: 190
        implicitHeight: 48
        readonly property bool muted: root.session && (root.session.muted || root.session.volume <= 0)
        RoundButton {
            id: muteBtn
            size: 48
            icon: vol.muted ? "mute" : "volume"
            active: vol.muted
            tooltip: "Mute"
            onTapped: if (root.session) root.session.setMuted(!root.session.muted)
        }
        Item {
            id: volBar
            anchors.left: muteBtn.right
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 6
            height: 20
            readonly property real frac: root.session ? Math.max(0, Math.min(1, root.session.volume)) : 0
            Rectangle {
                id: volTrack
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: volArea.containsMouse ? 8 : 6
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.16)
                Behavior on height { NumberAnimation { duration: 90 } }
                Rectangle {
                    height: parent.height
                    radius: height / 2
                    width: parent.width * volBar.frac
                    color: root.theme ? root.theme.gold : "#f0c44a"
                }
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: root.theme ? root.theme.gold : "#f0c44a"
                anchors.verticalCenter: parent.verticalCenter
                x: volTrack.width * volBar.frac - width / 2
                visible: !vol.muted
            }
            MouseArea {
                id: volArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                function apply(x) {
                    if (root.session) {
                        root.session.setMuted(false)
                        root.session.setVolume(Math.max(0, Math.min(1, x / Math.max(1, width))))
                    }
                }
                onPressed: function(m) { apply(m.x) }
                onPositionChanged: function(m) { if (pressed) apply(m.x) }
            }
        }
    }

    // ---- state line --------------------------------------------------------------------------------
    Text {
        id: stateLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 54
        anchors.rightMargin: 54
        height: 22
        verticalAlignment: Text.AlignVCenter
        color: root.theme ? root.theme.ink : "#f7f7f5"
        font.family: "Segoe UI"
        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: root.buffering ? "Buffering" : (root.paused ? "Paused"
              : (seekBar.seeking ? "Seek  " + root.fmt(seekBar.previewSeconds) : ""))
    }

    // ---- seek row: elapsed  [seek bar]  duration/remaining ---------------------------------------
    Item {
        id: seekRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: stateLine.bottom
        anchors.leftMargin: 54
        anchors.rightMargin: 54
        height: 42

        Text {
            id: elapsed
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 58
            text: root.fmt(seekBar.shownPos)
            color: root.theme ? root.theme.ink : "#f7f7f5"
            font.family: "Segoe UI"
            font.pixelSize: 13
            font.features: ({ "tnum": 1 })
        }
        SeekBar {
            id: seekBar
            session: root.session
            theme: root.theme
            anchors.left: elapsed.right
            anchors.right: duration.left
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: duration
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 58
            horizontalAlignment: Text.AlignRight
            text: root.showRemaining ? "-" + root.fmt(Math.max(0, root.dur - seekBar.shownPos))
                                     : root.fmt(root.dur)
            color: root.theme ? root.theme.inkDim : "#c9c8d0"
            font.family: "Segoe UI"
            font.pixelSize: 13
            font.features: ({ "tnum": 1 })
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.showRemaining = !root.showRemaining
            }
        }
    }

    // ---- transport buttons ------------------------------------------------------------------------
    Item {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: seekRow.bottom
        anchors.leftMargin: 54
        anchors.rightMargin: 54
        anchors.topMargin: 4
        height: 56

        VolumeControl {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        Row {
            anchors.centerIn: parent
            spacing: 8
            RoundButton {
                size: 40; icon: "seekBack"; seconds: String(root.seekStepSeconds)
                tooltip: "Skip back"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: if (root.session) root.session.seekRelative(-root.seekStepSeconds)
            }
            RoundButton {
                size: 48; hero: true
                icon: (root.playing || root.buffering) ? "pause" : "play"
                tooltip: (root.playing || root.buffering) ? "Pause" : "Play"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: root.togglePlayPause()
            }
            RoundButton {
                size: 40; icon: "seekForward"; seconds: String(root.seekStepSeconds)
                tooltip: "Skip forward"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: if (root.session) root.session.seekRelative(root.seekStepSeconds)
            }
        }

        RoundButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            size: 40; icon: "fullscreen"; tooltip: "Fullscreen"
            onTapped: root.fullscreenRequested()
        }
    }
}

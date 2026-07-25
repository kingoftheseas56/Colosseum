import QtQuick

// The bottom control dock: a state line, the seek row, and the transport buttons. It renders typed
// session state and sends typed session commands only. Layout and palette track the current player.
Item {
    id: root

    property var session
    property QtObject theme
    property bool showRemaining: false
    property string endsAtClock: ""   // "11:42 PM" wall-clock finish time (computed by the shell)
    property int currentAudioIndex: -1
    property int currentSubtitleIndex: -1
    readonly property bool anyMenuOpen: audioMenu.open || subtitleMenu.open || fillMenu.open
                                        || speedMenu.open
    property bool hasPrevEpisode: false
    property bool hasNextEpisode: false
    property bool windowed: true   // host-fed window state; drives the fullscreen/exit icon (parity)
    // Playback-speed presets (parity with the current player's speedChoices).
    readonly property var speedChoices: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    readonly property real currentSpeed: session ? session.speed : 1.0
    signal fullscreenRequested()
    signal browseRequested()
    signal prevEpisodeRequested()
    signal nextEpisodeRequested()

    function closeMenus() {
        audioMenu.open = false
        subtitleMenu.open = false
        fillMenu.open = false
        speedMenu.open = false
    }

    // Aspect / fill modes — a first-class bottom-bar control (parity: the current player keeps this as a
    // HUD chip, folding it into the overflow only when the bar is too narrow). Applying a mode sends the
    // three typed session commands; C++ owns the actual scaling.
    property int fillIndex: 0
    readonly property var fillModes: [
        { name: "Fit",  aspect: "",     panscan: 0.0, zoom: 0.0 },
        { name: "Fill", aspect: "",     panscan: 1.0, zoom: 0.0 },
        { name: "Zoom", aspect: "",     panscan: 0.0, zoom: 0.35 },
        { name: "16:9", aspect: "16:9", panscan: 0.0, zoom: 0.0 },
        { name: "4:3",  aspect: "4:3",  panscan: 0.0, zoom: 0.0 }
    ]
    function applyFill(i) {
        fillIndex = i
        var m = fillModes[i]
        if (session) {
            session.setVideoAspect(m.aspect)
            session.setPanscan(m.panscan)
            session.setVideoZoom(m.zoom)
        }
        fillMenu.open = false
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onAudioTrackChanged(generation, streamIndex) { root.currentAudioIndex = streamIndex }
        function onSubtitleTrackChanged(generation, streamIndex) { root.currentSubtitleIndex = streamIndex }
    }

    implicitHeight: 126

    readonly property int seekStepSeconds: 10
    readonly property bool playing: session && session.state === 3 // Player2State::Playing
    // Waiting on bytes. Two shapes, one meaning: the session sat down in Buffering(2), OR it is in
    // Seeking(5) and the source says it is stalled. A seek into a torrent's not-yet-downloaded bytes
    // is the second shape - the engine waits on purpose and the state never leaves Seeking, so
    // without networkStalled the whole wait rendered as the PREVIOUS play/pause state, i.e. nothing.
    readonly property bool buffering: session
        && (session.state === 2 // Player2State::Buffering
            || (session.state === 5 && session.networkStalled)) // Seeking, waiting on the origin
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

    // ---- state row: left = what the player is doing, right = the wall-clock finish time -----------
    Item {
        id: stateRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 54
        anchors.rightMargin: 54
        height: 22
        Text {
            id: stateLine
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: root.theme ? root.theme.ink : "#f7f7f5"
            font.family: "Segoe UI"
            font.pixelSize: 14
            font.weight: Font.DemiBold
            text: root.buffering ? "Buffering" : (root.paused ? "Paused"
                  : (seekBar.seeking ? "Seek  " + root.fmt(seekBar.previewSeconds) : ""))
        }
        Text {
            id: endsLabel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.endsAtClock.length > 0
            text: "ENDS  " + root.endsAtClock
            color: root.theme ? root.theme.inkDim : "#c9c8d0"
            font.family: "Segoe UI"; font.pixelSize: 12; font.letterSpacing: 1.5
            font.features: ({ "tnum": 1 })
        }
    }

    // ---- seek row: elapsed  [seek bar]  duration/remaining ---------------------------------------
    Item {
        id: seekRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: stateRow.bottom
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

        // LEFT cluster: volume, then aspect/fill — fill's home is the LEFT side per the current
        // player's approved placement ("Main HUD icon (Hemanth 2026-07-18), LEFT cluster").
        Row {
            id: leftCluster
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            VolumeControl {
                anchors.verticalCenter: parent.verticalCenter
            }
            RoundButton {
                id: fillButton
                size: 40; icon: "fit"
                active: fillMenu.open || root.fillIndex !== 0
                tooltip: "Aspect ratio"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: {
                    audioMenu.open = false; subtitleMenu.open = false; speedMenu.open = false
                    fillMenu.open = !fillMenu.open
                }
            }
        }

        Row {
            anchors.centerIn: parent
            spacing: 8
            RoundButton {
                visible: root.hasPrevEpisode
                size: 40; icon: "prevEpisode"; tooltip: "Previous episode"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: root.prevEpisodeRequested()
            }
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
            RoundButton {
                visible: root.hasNextEpisode
                size: 40; icon: "nextEpisode"; tooltip: "Next episode"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: root.nextEpisodeRequested()
            }
        }

        Row {
            id: rightCluster
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            RoundButton {
                size: 40; icon: "episodes"; tooltip: "Episodes & sources (E)"
                onTapped: { root.closeMenus(); root.browseRequested() }
            }
            RoundButton {
                size: 40; icon: "audio"; active: audioMenu.open; tooltip: "Audio track"
                onTapped: { subtitleMenu.open = false; audioMenu.open = !audioMenu.open }
            }
            RoundButton {
                size: 40; icon: "subtitle"; active: subtitleMenu.open; tooltip: "Subtitles"
                onTapped: { audioMenu.open = false; subtitleMenu.open = !subtitleMenu.open }
            }
            // Playback speed — icon button with a small gold badge showing the rate when non-default,
            // exactly like the current player's SpeedMenuButton (speed portion; sleep/skip not ported).
            Item {
                id: speedButton
                width: 40; height: 40
                anchors.verticalCenter: parent.verticalCenter
                readonly property bool nonDefault: Math.abs(root.currentSpeed - 1) > 0.001
                RoundButton {
                    anchors.fill: parent
                    size: 40; icon: "speed"
                    active: speedMenu.open || speedButton.nonDefault
                    tooltip: "Speed"
                    onTapped: {
                        audioMenu.open = false; subtitleMenu.open = false; fillMenu.open = false
                        speedMenu.open = !speedMenu.open
                    }
                }
                Rectangle {
                    visible: speedButton.nonDefault
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.rightMargin: 1; anchors.bottomMargin: 3
                    width: spdVal.implicitWidth + 6; height: 13; radius: 6.5
                    color: root.theme ? root.theme.gold : "#f0c44a"
                    Text {
                        id: spdVal
                        anchors.centerIn: parent
                        text: (Math.round(root.currentSpeed * 100) / 100) + "×"
                        color: "#101014"
                        font.family: "Segoe UI"; font.features: ({ "tnum": 1 })
                        font.pixelSize: 8; font.weight: Font.DemiBold
                    }
                }
            }
            RoundButton {
                size: 40
                icon: root.windowed ? "fullscreen" : "fullscreenExit"
                tooltip: root.windowed ? "Enter fullscreen (F)" : "Exit fullscreen (F)"
                onTapped: root.fullscreenRequested()
            }
        }

        TrackMenu {
            id: audioMenu
            theme: root.theme
            title: "Audio"
            tracks: root.session ? root.session.audioTracks : []
            selectedIndex: root.currentAudioIndex
            syncValue: root.session ? root.session.audioDelay : 0
            anchors.right: rightCluster.right
            anchors.bottom: rightCluster.top
            anchors.bottomMargin: 12
            onPicked: function(streamIndex) {
                if (root.session) root.session.selectAudioTrack(String(streamIndex))
                audioMenu.open = false
            }
            onSyncChanged: function(seconds) { if (root.session) root.session.setAudioDelay(seconds) }
        }
        TrackMenu {
            id: subtitleMenu
            theme: root.theme
            title: "Subtitles"
            allowOff: true
            tracks: root.session ? root.session.subtitleTracks : []
            selectedIndex: root.currentSubtitleIndex
            syncValue: root.session ? root.session.subDelay : 0
            anchors.right: rightCluster.right
            anchors.bottom: rightCluster.top
            anchors.bottomMargin: 12
            onPicked: function(streamIndex) {
                if (root.session)
                    root.session.selectSubtitleTrack(streamIndex < 0 ? "off" : String(streamIndex))
                subtitleMenu.open = false
            }
            onSyncChanged: function(seconds) { if (root.session) root.session.setSubDelay(seconds) }
        }

        // Fill/aspect popover — ported from the current player's FillMenuButton panel: a "Video" title,
        // centred mode rows (selected in gold), and a little arrow pointer, centred directly above the
        // fill button.
        Rectangle {
            id: fillMenu
            property bool open: false
            width: 188
            height: 56 + root.fillModes.length * 34
            // Centred over the fill button in the LEFT cluster — bound to the button's real geometry
            // (clamped to the bar), not a hand-tuned offset that rots when the roster changes.
            x: Math.max(10, leftCluster.x + fillButton.x + fillButton.width / 2 - width / 2)
            anchors.bottom: leftCluster.top
            anchors.bottomMargin: 12
            visible: open
            opacity: open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            radius: 14
            color: root.theme ? root.theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            MouseArea { anchors.fill: parent; hoverEnabled: true } // absorb background clicks

            Rectangle {
                width: 8; height: 8; rotation: 45
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom
                color: parent.color
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14)
            }

            Text {
                x: 18; y: 15
                text: "Video"
                color: root.theme ? root.theme.ink : "#f7f7f5"
                font.family: "Segoe UI"; font.pixelSize: 14; font.weight: Font.DemiBold
            }

            Repeater {
                model: root.fillModes
                Rectangle {
                    required property int index
                    required property var modelData
                    x: 8; y: 48 + index * 34
                    width: parent.width - 16; height: 32; radius: 8
                    readonly property bool selected: root.fillIndex === index
                    color: selected ? Qt.rgba(1, 1, 1, 0.10)
                         : (fillRowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    Text {
                        anchors.centerIn: parent
                        text: modelData.name
                        color: parent.selected ? (root.theme ? root.theme.gold : "#f0c44a")
                                               : (root.theme ? root.theme.ink : "#f7f7f5")
                        font.family: "Segoe UI"; font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: fillRowArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.applyFill(index)
                    }
                }
            }
        }

        // Speed popover — ported from the current player's SpeedMenuButton panel (speed column only;
        // sleep timer + skip step are accepted exceptions): uppercase "PLAYBACK SPEED" eyebrow,
        // left-aligned rows (selected = gold + border), a DEFAULT hint on the Normal row, arrow pointer.
        Rectangle {
            id: speedMenu
            property bool open: false
            width: 210
            height: 46 + root.speedChoices.length * 38 + 8
            // Centred over the speed button — bound to the button's real geometry, clamped to the bar.
            x: Math.min(parent.width - width - 10,
                        rightCluster.x + speedButton.x + speedButton.width / 2 - width / 2)
            anchors.bottom: rightCluster.top
            anchors.bottomMargin: 12
            visible: open
            opacity: open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            radius: 14
            color: root.theme ? root.theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            MouseArea { anchors.fill: parent; hoverEnabled: true } // absorb background clicks

            Rectangle {
                width: 8; height: 8; rotation: 45
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom
                color: parent.color
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14)
            }

            // Harbor's exact section title (uppercase eyebrow) — matches the current player.
            Text {
                x: 18; y: 16
                text: "Playback speed"
                color: root.theme ? root.theme.inkDimmer : "#9a99a5"
                font.family: "Segoe UI"; font.pixelSize: 11; font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.6
            }

            Repeater {
                model: root.speedChoices
                Rectangle {
                    required property int index
                    required property real modelData
                    x: 8; y: 46 + index * 38
                    width: parent.width - 16; height: 36; radius: 9
                    readonly property bool selected: Math.abs(root.currentSpeed - modelData) < 0.01
                    color: selected ? Qt.rgba(1, 1, 1, 0.10)
                         : (speedRowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: selected ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    // "Normal" for 1×, else "1.25×" — left-aligned (current-player parity).
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.abs(modelData - 1) < 0.01 ? "Normal"
                              : ((Math.round(modelData * 100) / 100) + "×")
                        color: parent.selected ? (root.theme ? root.theme.gold : "#f0c44a")
                                               : (root.theme ? root.theme.ink : "#f7f7f5")
                        font.family: "Segoe UI"; font.pixelSize: 14
                        font.weight: parent.selected ? Font.DemiBold : Font.Medium
                    }
                    Text {
                        visible: Math.abs(modelData - 1) < 0.01
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: "DEFAULT"
                        color: root.theme ? root.theme.inkDimmer : "#9a99a5"
                        font.family: "Segoe UI"; font.pixelSize: 10; font.letterSpacing: 1.4
                    }
                    MouseArea {
                        id: speedRowArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.session) root.session.speed = modelData
                            speedMenu.open = false
                        }
                    }
                }
            }
        }
    }
}

import QtQuick
import "../.."

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
    // Host-fed, because the shell owns orchestration and this bar only paints. Production gates the
    // same two buttons on "there is more than one candidate" and "there is a URL to download"
    // (PlayerPage.qml:4611, 4620), which is also why neither appears for a local file.
    // Production's own narrow-window measure: the room left for the utility clusters once the centre
    // transport has taken the middle (PlayerPage's `utilitySpace` / `barTiny`). Exposed so surfaces
    // that fold WITH the dock - the pause card - fold at the same moment the dock does, instead of
    // guessing from their own width.
    readonly property real utilitySpace: root.width / 2 - centreCluster.width / 2 - 34
    readonly property bool barTiny: root.utilitySpace < 260
    property bool canSwitchSource: false
    property bool canDownload: false
    property string downloadKind: "idle"   // idle | queued | downloading | done | failed
    property string downloadTooltip: "Download video"
    property bool windowed: true   // host-fed window state; drives the fullscreen/exit icon (parity)
    // Playback-speed presets (parity with the current player's speedChoices).
    readonly property var speedChoices: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    readonly property real currentSpeed: session ? session.speed : 1.0
    signal fullscreenRequested()
    signal browseRequested()
    // The HUD's own source switch: his 2026-07-26 ruling kept the drawer as the full switching
    // surface AND asked for this button back ("I would still like the source switch button, if for
    // nothing else, for how cool that lucide icon looks"), so it is a shortcut INTO the drawer's
    // Sources tab rather than a second, competing switcher.
    signal switchSourceRequested()
    signal downloadRequested()
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
    // During a stalled seek `buffering` is true, so this button renders as a live Pause - and the
    // press IS honoured: Player2Session::pause() sees the Seeking state and steers m_postSeekState,
    // so the seek completes paused. play() is symmetric. Neither takes the Seeking -> Playing
    // transition directly, which is legal but would start playback before seekCompleted lands and
    // race the seek's own completion. The press lands; it just lands when the seek does.
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
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: rb.tooltip
            focusRadius: rb.size / 2
            onTriggered: rb.tapped()
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
            readonly property real value: root.session ? root.session.volume * 100 : 0
            readonly property real minimumValue: 0
            readonly property real maximumValue: 100
            readonly property real stepSize: 5
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
            focusPolicy: root.session ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) {
                if (!root.session) return
                var step = (event.modifiers & Qt.ShiftModifier) ? 0.01 : 0.05
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) { root.session.setMuted(false); root.session.setVolume(Math.max(0, root.session.volume - step)); event.accepted = true }
                else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) { root.session.setMuted(false); root.session.setVolume(Math.min(1, root.session.volume + step)); event.accepted = true }
                else if (event.key === Qt.Key_Home) { root.session.setMuted(false); root.session.setVolume(0); event.accepted = true }
                else if (event.key === Qt.Key_End) { root.session.setMuted(false); root.session.setVolume(1); event.accepted = true }
            }
            Accessible.role: Accessible.Slider
            Accessible.name: "Volume"
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
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: root.showRemaining ? "Show total duration" : "Show remaining time"; onTriggered: root.showRemaining = !root.showRemaining }
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
            // Order matches the shipped player's leftUtilityRow: volume, [retry], stream, download,
            // fill (PlayerPage.qml:4588-4642). Size is 40 to match this bar's own cluster rather
            // than production's 48 — every sibling here is 40, and a lone 48 would read as a
            // mistake. That size gap is a pre-existing, separate drift and is logged as one.
            RoundButton {
                id: sourceButton
                size: 40
                // "stream" maps to Lucide `replace` in Player2Icon, the SAME glyph the shipped
                // player uses (PlayerIcon.qml:39). His words: no substitute.
                icon: "stream"
                tooltip: "Pick another stream"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.canSwitchSource
                onTapped: root.switchSourceRequested()
            }
            RoundButton {
                id: downloadButton
                size: 40
                // Same four-state glyph vocabulary as the shipped player (PlayerPage.qml:1651-1662).
                icon: root.downloadKind === "downloading" ? "cancel"
                      : root.downloadKind === "done" ? "check"
                      : root.downloadKind === "failed" ? "warning" : "download"
                // Gold while a download is anything but idle, exactly as production tints it.
                active: root.downloadKind !== "idle"
                tooltip: root.downloadTooltip
                anchors.verticalCenter: parent.verticalCenter
                visible: root.canDownload
                onTapped: root.downloadRequested()
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
            id: centreCluster
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
            property var focusReturnItem: null
            property int keyboardIndex: Math.max(0, root.fillIndex)
            focusPolicy: open ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    var next = keyboardIndex + (event.key === Qt.Key_Up ? -1 : 1)
                    if (next >= 0 && next < root.fillModes.length) { keyboardIndex = next; event.accepted = true }
                } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
                else if (event.key === Qt.Key_End) { keyboardIndex = root.fillModes.length - 1; event.accepted = true }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { root.applyFill(keyboardIndex); event.accepted = true }
                else if (event.key === Qt.Key_Escape) { open = false; event.accepted = true }
            }
            Keys.onTabPressed: function(event) { fillMenu.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
            Keys.onBacktabPressed: function(event) { fillMenu.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
            onOpenChanged: {
                if (open) {
                    var w = root.Window.window; focusReturnItem = w ? w.activeFocusItem : null
                    keyboardIndex = Math.max(0, root.fillIndex)
                    Qt.callLater(function() { fillMenu.forceActiveFocus(Qt.PopupFocusReason) })
                } else if (focusReturnItem) {
                    var target = focusReturnItem; focusReturnItem = null
                    Qt.callLater(function() { if (target && target.visible && target.enabled && target.forceActiveFocus) target.forceActiveFocus(Qt.TabFocusReason) })
                }
            }
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
                id: fillRepeater
                model: root.fillModes
                Rectangle {
                    required property int index
                    required property var modelData
                    x: 8; y: 48 + index * 34
                    width: parent.width - 16; height: 32; radius: 8
                    readonly property bool selected: root.fillIndex === index
                    border.width: fillMenu.activeFocus && fillMenu.keyboardIndex === index ? 2 : 0
                    border.color: root.theme ? root.theme.gold : "#f0c44a"
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
            property var focusReturnItem: null
            property int keyboardIndex: 0
            focusPolicy: open ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    var next = keyboardIndex + (event.key === Qt.Key_Up ? -1 : 1)
                    if (next >= 0 && next < root.speedChoices.length) { keyboardIndex = next; event.accepted = true }
                } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
                else if (event.key === Qt.Key_End) { keyboardIndex = root.speedChoices.length - 1; event.accepted = true }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { if (root.session) root.session.speed = root.speedChoices[keyboardIndex]; open = false; event.accepted = true }
                else if (event.key === Qt.Key_Escape) { open = false; event.accepted = true }
            }
            Keys.onTabPressed: function(event) { speedMenu.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
            Keys.onBacktabPressed: function(event) { speedMenu.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
            onOpenChanged: {
                if (open) {
                    var w = root.Window.window; focusReturnItem = w ? w.activeFocusItem : null
                    var idx = 0; for (var i = 0; i < root.speedChoices.length; ++i) if (Math.abs(root.currentSpeed - root.speedChoices[i]) < 0.01) { idx = i; break }
                    keyboardIndex = idx
                    Qt.callLater(function() { speedMenu.forceActiveFocus(Qt.PopupFocusReason) })
                } else if (focusReturnItem) {
                    var target = focusReturnItem; focusReturnItem = null
                    Qt.callLater(function() { if (target && target.visible && target.enabled && target.forceActiveFocus) target.forceActiveFocus(Qt.TabFocusReason) })
                }
            }
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
                id: speedRepeater
                model: root.speedChoices
                Rectangle {
                    required property int index
                    required property real modelData
                    x: 8; y: 46 + index * 38
                    width: parent.width - 16; height: 36; radius: 9
                    readonly property bool selected: Math.abs(root.currentSpeed - modelData) < 0.01
                    function choose() { if (root.session) root.session.speed = modelData; speedMenu.open = false }
                    border.width: (selected || (speedMenu.activeFocus && speedMenu.keyboardIndex === index)) ? (speedMenu.activeFocus && speedMenu.keyboardIndex === index ? 2 : 1) : 0
                    border.color: speedMenu.activeFocus && speedMenu.keyboardIndex === index ? (root.theme ? root.theme.gold : "#f0c44a") : Qt.rgba(1, 1, 1, 0.10)
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
                        onClicked: parent.choose()
                    }
                }
            }
        }
    }
}

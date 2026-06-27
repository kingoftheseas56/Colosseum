pragma ComponentBehavior: Bound

// PlayerPage - Harbor/TB3-style fullscreen player chrome on top of Colosseum's mpvqt MpvItem.
// Streaming remains behind the Stream.play -> streamReady seam; this file only owns player UI.
import QtQuick
import QtQuick.Window
import Colosseum.Player

Item {
    id: root
    anchors.fill: parent
    focus: true

    property Item backdrop
    property string mediaTitle: ""
    property string mediaSubtitle: ""
    property bool starting: false
    property bool errored: false
    property string statusMsg: ""
    property bool controlsShown: true
    property bool seeking: false
    property real seekPreview: mpv.position
    property real seekBackSeconds: 10
    property real seekForwardSeconds: 10
    property real spaceBaseSpeed: 1
    property bool spaceHoldFired: false
    property int fillModeIndex: 0
    readonly property real chromeScaleY: {
        var w = root.Window.window
        return (w && w.screen && w.screen.devicePixelRatio > 0) ? w.screen.devicePixelRatio : 1
    }
    // NOTE: QML lays out in LOGICAL pixels; mpvqt composites correctly under QML overlays
    // regardless of devicePixelRatio. Dividing by DPR shrank the chrome box and parked the
    // transport mid-screen (the swallow it was meant to fix was actually a z-order bug, since
    // solved by chrome z:99999 over mpv z:-1). So chrome fills the TRUE window.
    readonly property real chromeVisibleWidth: width
    readonly property real chromeVisibleHeight: height
    readonly property bool compact: chromeVisibleWidth < 1000
    readonly property bool tight: chromeVisibleWidth < 680
    readonly property bool anyMenuOpen: audioMenu.panelOpen || subMenu.panelOpen || speedMenu.panelOpen || fillMenu.panelOpen
    readonly property var speedChoices: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    readonly property var fillModes: [
        { id: "fit", label: "Fit", panscan: 0, zoom: 0, aspect: "-1" },
        { id: "fill", label: "Fill", panscan: 1, zoom: 0, aspect: "-1" },
        { id: "zoom", label: "Zoom", panscan: 0, zoom: 0.35, aspect: "-1" },
        { id: "16:9", label: "16:9", panscan: 0, zoom: 0, aspect: "16:9" },
        { id: "4:3", label: "4:3", panscan: 0, zoom: 0, aspect: "4:3" },
        { id: "scope", label: "2.39:1", panscan: 0, zoom: 0, aspect: "2.39:1" }
    ]

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    function playTorrent(infoHash, fileIdx, title) {
        root.mediaTitle = title || ""
        root.mediaSubtitle = "Torrent stream"
        root.errored = false
        root.starting = true
        root.statusMsg = "Starting stream..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        Stream.play(infoHash, fileIdx)
    }

    function playUrl(url, title) {
        root.mediaTitle = title || ""
        root.mediaSubtitle = "Direct file"
        root.errored = false
        root.starting = true
        root.statusMsg = "Opening..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        mpv.loadFile(url)
    }

    function stop() {
        root.closeMenus()
        mpv.command(["stop"])
        root.starting = false
        root.errored = false
        root.statusMsg = ""
    }

    function fmtTime(sec) {
        if (!isFinite(sec) || sec < 0)
            return "0:00"
        var total = Math.floor(sec)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        var ss = s < 10 ? "0" + s : "" + s
        if (h > 0) {
            var mm = m < 10 ? "0" + m : "" + m
            return h + ":" + mm + ":" + ss
        }
        return m + ":" + ss
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function round2(v) { return Math.round(v * 100) / 100 }
    function seekFraction() {
        var pos = root.seeking ? root.seekPreview : mpv.position
        return mpv.duration > 0 ? root.clamp(pos / mpv.duration, 0, 1) : 0
    }
    function previewAt(mouseX, width) {
        return mpv.duration * root.clamp(mouseX / Math.max(1, width), 0, 1)
    }
    function seekTo(sec) {
        mpv.seekExact(root.clamp(sec, 0, Math.max(0, mpv.duration)))
        root.wakeChrome()
    }
    function seekStep(delta) {
        mpv.seekStep(delta)
        root.wakeChrome()
    }
    function togglePlayPause() {
        if (!root.starting && !root.errored)
            mpv.pause = !mpv.pause
        root.wakeChrome()
    }
    function setVolumeFromFraction(f) {
        var normalFraction = 0.62
        var next = f <= normalFraction
            ? (f / normalFraction) * 100
            : 100 + ((f - normalFraction) / (1 - normalFraction)) * 500
        mpv.volume = Math.round(root.clamp(next, 0, 600))
        if (mpv.volume > 0)
            mpv.mute = false
        root.wakeChrome()
    }
    function volumeFraction() {
        var v = root.clamp(mpv.volume, 0, 600)
        var normalFraction = 0.62
        if (v <= 100)
            return (v / 100) * normalFraction
        return normalFraction + ((v - 100) / 500) * (1 - normalFraction)
    }
    function closeMenus() {
        audioMenu.panelOpen = false
        subMenu.panelOpen = false
        speedMenu.panelOpen = false
        fillMenu.panelOpen = false
    }
    function wakeChrome() {
        root.controlsShown = true
        hideTimer.restart()
    }
    function applyFill(index) {
        root.fillModeIndex = root.clamp(index, 0, root.fillModes.length - 1)
        var mode = root.fillModes[root.fillModeIndex]
        mpv.panscan = mode.panscan
        mpv.videoZoom = mode.zoom
        mpv.videoAspect = mode.aspect
        fillMenu.panelOpen = false
        root.wakeChrome()
    }
    function cycleSubtitle() {
        var tracks = mpv.subtitleTracks
        if (!tracks || tracks.length === 0) {
            mpv.subtitleTrack = ""
            return
        }
        var idx = -1
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].selected === true || tracks[i].id === mpv.subtitleTrack) {
                idx = i
                break
            }
        }
        if (idx < 0)
            mpv.subtitleTrack = tracks[0].id
        else if (idx + 1 >= tracks.length)
            mpv.subtitleTrack = ""
        else
            mpv.subtitleTrack = tracks[idx + 1].id
    }
    function toggleWindowFullscreen() {
        var w = root.Window.window
        if (!w)
            return
        w.visibility = (w.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
        root.wakeChrome()
    }
    function trackTitle(track, fallback) {
        if (track.title && ("" + track.title).trim() !== "")
            return track.title
        if (track.lang && ("" + track.lang).trim() !== "")
            return ("" + track.lang).toUpperCase()
        return fallback
    }
    function trackMeta(track) {
        var parts = []
        if (track.lang && ("" + track.lang).trim() !== "")
            parts.push(("" + track.lang).toUpperCase())
        parts.push(track.external ? "External" : "Embedded")
        if (track.codec && ("" + track.codec).trim() !== "")
            parts.push(("" + track.codec).toUpperCase())
        if (track.forced)
            parts.push("Forced")
        if (track.default)
            parts.push("Default")
        return parts.join(" / ")
    }

    Component.onCompleted: {
        root.forceActiveFocus()
        root.wakeChrome()
    }
    onVisibleChanged: if (visible) root.forceActiveFocus()
    onStartingChanged: if (starting) root.wakeChrome()

    Theme { id: theme }

    Rectangle { anchors.fill: parent; z: -1; color: "#000000" }

    MpvItem {
        id: mpv
        anchors.fill: parent
        z: 0
        onFileStarted: {
            root.starting = true
            root.statusMsg = "Buffering..."
            root.wakeChrome()
        }
        onFileLoaded: {
            root.starting = false
            root.errored = false
            root.statusMsg = ""
            root.seekPreview = mpv.position
            root.wakeChrome()
        }
        onEndFile: function(reason) {
            root.starting = false
            if (reason === "error" || reason === "other") {
                root.errored = true
                root.statusMsg = "Playback failed."
                root.wakeChrome()
            }
        }
        onPauseChanged: if (mpv.pause) root.wakeChrome()
    }

    Connections {
        target: Stream
        function onStreamReady(url, infoHash, fileIdx) {
            root.statusMsg = "Buffering..."
            mpv.loadFile(url)
        }
        function onStreamError(message) {
            root.starting = false
            root.errored = true
            root.statusMsg = message
            root.wakeChrome()
        }
    }

    Timer {
        id: hideTimer
        interval: mpv.pause || root.starting ? 4500 : 1800
        repeat: false
        onTriggered: if (!mpv.pause && !root.starting && !root.seeking && !root.anyMenuOpen) root.controlsShown = false
    }

    Timer {
        id: spaceHoldTimer
        interval: 350
        repeat: false
        onTriggered: {
            root.spaceHoldFired = true
            mpv.speed = Math.max(2, root.spaceBaseSpeed)
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPositionChanged: root.wakeChrome()
        onClicked: {
            root.closeMenus()
            root.togglePlayPause()
        }
        onDoubleClicked: root.toggleWindowFullscreen()
    }

    Keys.onPressed: function(event) {
        root.wakeChrome()
        if (event.key === Qt.Key_Space) {
            event.accepted = true
            if (event.isAutoRepeat)
                return
            root.spaceBaseSpeed = mpv.speed
            root.spaceHoldFired = false
            spaceHoldTimer.restart()
            return
        }
        if (event.key === Qt.Key_Escape) {
            event.accepted = true
            if (root.anyMenuOpen)
                root.closeMenus()
            else
                root.backRequested()
            return
        }
        if (event.key === Qt.Key_Left) { event.accepted = true; root.seekStep(-root.seekBackSeconds); return }
        if (event.key === Qt.Key_Right) { event.accepted = true; root.seekStep(root.seekForwardSeconds); return }
        if (event.key === Qt.Key_Comma) { event.accepted = true; root.seekStep(-30); return }
        if (event.key === Qt.Key_Period) { event.accepted = true; root.seekStep(30); return }
        if (event.key === Qt.Key_Home) { event.accepted = true; root.seekTo(0); return }
        if (event.key === Qt.Key_End && mpv.duration > 0) { event.accepted = true; root.seekTo(mpv.duration - 0.5); return }
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            event.accepted = true
            var digit = event.key - Qt.Key_0
            root.seekTo(digit === 0 ? 0 : mpv.duration * digit / 10)
            return
        }
        if (event.key === Qt.Key_F) { event.accepted = true; root.toggleWindowFullscreen(); return }
        if (event.key === Qt.Key_M) { event.accepted = true; mpv.mute = !mpv.mute; return }
        if (event.key === Qt.Key_Up) { event.accepted = true; mpv.volume = mpv.volume + (event.modifiers & Qt.ShiftModifier ? 50 : 5); return }
        if (event.key === Qt.Key_Down) { event.accepted = true; mpv.volume = mpv.volume - (event.modifiers & Qt.ShiftModifier ? 50 : 5); return }
        if (event.key === Qt.Key_BracketLeft) { event.accepted = true; mpv.speed = root.clamp(root.round2(mpv.speed - 0.25), 0.25, 3); return }
        if (event.key === Qt.Key_BracketRight) { event.accepted = true; mpv.speed = root.clamp(root.round2(mpv.speed + 0.25), 0.25, 3); return }
        if (event.key === Qt.Key_Z) { event.accepted = true; mpv.subDelay = root.round2(mpv.subDelay - (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return }
        if (event.key === Qt.Key_X) { event.accepted = true; mpv.subDelay = root.round2(mpv.subDelay + (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return }
        if (event.key === Qt.Key_S || event.key === Qt.Key_C) { event.accepted = true; root.cycleSubtitle(); return }
    }

    Keys.onReleased: function(event) {
        if (event.key !== Qt.Key_Space)
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        if (spaceHoldTimer.running)
            spaceHoldTimer.stop()
        if (root.spaceHoldFired)
            mpv.speed = root.spaceBaseSpeed
        else
            root.togglePlayPause()
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: root.starting || root.errored
        z: 4

        Item {
            width: 48
            height: 48
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.starting && !root.errored
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 3
                border.color: Qt.rgba(1, 1, 1, 0.18)
            }
            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: theme.gold
                x: parent.width / 2 - 6
                y: -1
            }
            RotationAnimation on rotation {
                running: root.starting
                loops: Animation.Infinite
                from: 0
                to: 360
                duration: 900
            }
        }
        Text {
            width: Math.min(root.width - 120, 520)
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.statusMsg
            color: root.errored ? "#e6a3a3" : theme.ink
            font.family: theme.ui
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        id: chrome
        x: 0
        y: 0
        width: root.chromeVisibleWidth
        height: root.chromeVisibleHeight
        z: 99999
        color: Qt.rgba(0, 0, 0, 0.001)
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 142
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.68) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.24) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        Rectangle {
            x: 0
            y: root.chromeVisibleHeight - height
            width: root.chromeVisibleWidth
            height: 236
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 0.38; color: Qt.rgba(0, 0, 0, 0.28) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.78) }
            }
        }

        RoundButton {
            id: backButton
            x: tight ? 16 : 28
            y: tight ? 14 : 20
            size: 48
            icon: "back"
            tooltip: "Back"
            onClicked: root.backRequested()
        }

        Column {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: tight ? 18 : 24
            anchors.rightMargin: tight ? 18 : 34
            spacing: 3
            width: Math.min(560, parent.width - (tight ? 92 : 140))
            Text {
                width: parent.width
                text: root.mediaTitle || mpv.mediaTitle
                color: theme.ink
                font.family: theme.display
                font.pixelSize: tight ? 18 : 22
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.55)
            }
            Text {
                width: parent.width
                text: root.mediaSubtitle
                visible: text.length > 0
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.55)
            }
        }

        Rectangle {
            id: bottomDockLayer
            z: 3
            x: 0
            y: 0
            width: parent.width
            height: parent.height
            color: "transparent"

            Rectangle {
                id: bottomDock
                layer.enabled: true
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: tight ? 14 : 28
            anchors.rightMargin: tight ? 14 : 28
            anchors.bottomMargin: tight ? 12 : 22
            height: tight ? 130 : 156
            radius: 22
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.50)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            Row {
                id: seekRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: tight ? 16 : 22
                anchors.rightMargin: tight ? 16 : 22
                anchors.topMargin: 16
                height: 42
                spacing: 12

                Text {
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(root.seeking ? root.seekPreview : mpv.position)
                    color: theme.ink
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }

                Item {
                    id: seekBar
                    width: seekRow.width - (tight ? 0 : 140)
                    height: parent.height
                    property bool hovered: false
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: seekBar.hovered || root.seeking ? 8 : 6
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * root.seekFraction()
                        height: seekBar.hovered || root.seeking ? 8 : 6
                        radius: height / 2
                        color: theme.gold
                    }
                    Rectangle {
                        x: parent.width * root.seekFraction() - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.seeking ? 20 : 16
                        height: width
                        radius: width / 2
                        color: theme.gold
                        border.width: 1
                        border.color: Qt.rgba(0, 0, 0, 0.32)
                        visible: mpv.duration > 0
                    }
                    Rectangle {
                        visible: seekBar.hovered && !root.seeking && mpv.duration > 0
                        x: root.clamp(seekHover.mouseX - width / 2, 0, parent.width - width)
                        y: -30
                        width: previewText.implicitWidth + 16
                        height: 28
                        radius: 7
                        color: Qt.rgba(0, 0, 0, 0.86)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.10)
                        Text {
                            id: previewText
                            anchors.centerIn: parent
                            text: root.fmtTime(root.seekPreview)
                            color: theme.ink
                            font.family: "Consolas"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                    MouseArea {
                        id: seekHover
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: mpv.duration > 0
                        cursorShape: Qt.PointingHandCursor
                        onEntered: { seekBar.hovered = true; root.wakeChrome() }
                        onExited: {
                            seekBar.hovered = false
                            if (!root.seeking)
                                root.seekPreview = mpv.position
                        }
                        onPositionChanged: {
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                        }
                        onPressed: {
                            root.seeking = true
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                        }
                        onReleased: {
                            root.seekTo(root.seekPreview)
                            root.seeking = false
                        }
                    }
                }

                Text {
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(mpv.duration)
                    color: theme.inkDim
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignRight
                }
            }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: tight ? 16 : 22
                anchors.rightMargin: tight ? 16 : 22
                anchors.bottomMargin: 16
                height: 64

                VolumeControl {
                    id: volumeControl
                    visible: !tight
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                Row {
                    anchors.centerIn: parent
                    spacing: compact ? 6 : 8
                    RoundButton {
                        size: tight ? 48 : 56
                        icon: "seekBack"
                        label: root.seekBackSeconds
                        tooltip: "Back " + root.seekBackSeconds + "s"
                        onClicked: root.seekStep(-root.seekBackSeconds)
                    }
                    RoundButton {
                        size: tight ? 54 : 64
                        icon: mpv.pause ? "play" : "pause"
                        hero: true
                        tooltip: mpv.pause ? "Play" : "Pause"
                        onClicked: root.togglePlayPause()
                    }
                    RoundButton {
                        size: tight ? 48 : 56
                        icon: "seekForward"
                        label: root.seekForwardSeconds
                        tooltip: "Forward " + root.seekForwardSeconds + "s"
                        onClicked: root.seekStep(root.seekForwardSeconds)
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    PlayerMenu {
                        id: audioMenu
                        visible: !tight
                        icon: "audio"
                        title: "Audio"
                        count: mpv.audioTracks.length
                        panelWidth: 360
                        panelHeight: Math.min(310, 86 + Math.max(1, mpv.audioTracks.length) * 48 + 42)
                        delegateModel: mpv.audioTracks
                        emptyText: "No alternate audio tracks in this file."
                        syncValue: mpv.audioDelay
                        onTrackPicked: function(trackId) { mpv.audioTrack = trackId }
                        onDelayStep: function(delta) { mpv.audioDelay = root.round2(mpv.audioDelay + delta) }
                        onResetDelay: mpv.audioDelay = 0
                    }

                    PlayerMenu {
                        id: subMenu
                        icon: "subtitle"
                        title: "Subtitles"
                        count: mpv.subtitleTracks.length
                        panelWidth: 380
                        panelHeight: Math.min(336, 124 + Math.max(1, mpv.subtitleTracks.length) * 48 + 42)
                        delegateModel: mpv.subtitleTracks
                        emptyText: "No embedded subtitles in this file."
                        offRow: true
                        syncValue: mpv.subDelay
                        active: mpv.subtitleTrack !== ""
                        onTrackPicked: function(trackId) { mpv.subtitleTrack = trackId }
                        onOffPicked: mpv.subtitleTrack = ""
                        onDelayStep: function(delta) { mpv.subDelay = root.round2(mpv.subDelay + delta) }
                        onResetDelay: mpv.subDelay = 0
                    }

                    SpeedMenuButton {
                        id: speedMenu
                        visible: !compact
                    }

                    FillMenuButton {
                        id: fillMenu
                        visible: !compact
                    }

                    RoundButton {
                        size: 48
                        icon: "fullscreen"
                        tooltip: "Fullscreen"
                        onClicked: root.toggleWindowFullscreen()
                    }
                }
            }
        }
        }

    }

    component RoundButton: Item {
        id: rb
        property int size: 48
        property string icon: ""
        property string label: ""
        property string tooltip: ""
        property bool hero: false
        property bool active: false
        signal clicked()
        width: size
        height: size
        scale: press.pressed ? 0.95 : (press.containsMouse ? 1.04 : 1)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: rb.hero ? (press.containsMouse ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.13))
                           : rb.active ? Qt.rgba(1, 1, 1, 0.16)
                           : press.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
            border.width: rb.hero || rb.active ? 1 : 0
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }
        IconGlyph {
            anchors.fill: parent
            kind: rb.icon
            label: rb.label
            hero: rb.hero
            ink: rb.active ? theme.gold : theme.ink
        }
        MouseArea {
            id: press
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: root.wakeChrome()
            onClicked: rb.clicked()
        }
    }

    component VolumeControl: Item {
        id: vc
        width: mpv.volume > 101 ? 230 : 190
        height: 48
        RoundButton {
            id: muteButton
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            size: 48
            icon: mpv.mute || mpv.volume <= 0 ? "mute" : "volume"
            active: mpv.mute
            tooltip: "Mute"
            onClicked: mpv.mute = !mpv.mute
        }
        Item {
            id: volBar
            anchors.left: muteButton.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: vc.width - muteButton.width - 12
            height: 34
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: volMouse.containsMouse ? 8 : 6
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.16)
            }
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * root.volumeFraction()
                height: volMouse.containsMouse ? 8 : 6
                radius: height / 2
                color: mpv.volume > 100 ? "#f26f25" : theme.gold
            }
            Rectangle {
                x: parent.width * root.volumeFraction() - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: mpv.volume > 100 ? "#f26f25" : theme.gold
            }
            Text {
                visible: mpv.volume > 100
                anchors.left: parent.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(mpv.volume) + "%"
                color: "#f26f25"
                font.family: "Consolas"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            MouseArea {
                id: volMouse
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                function apply() { root.setVolumeFromFraction(mouseX / Math.max(1, volBar.width)) }
                onEntered: root.wakeChrome()
                onPressed: apply()
                onPositionChanged: if (pressed) apply()
            }
        }
    }

    component PlayerMenu: Item {
        id: menu
        property bool panelOpen: false
        property string icon: ""
        property string title: ""
        property int count: 0
        property int panelWidth: 360
        property int panelHeight: 280
        property var delegateModel: []
        property string emptyText: ""
        property bool offRow: false
        property bool active: false
        property real syncValue: 0
        signal trackPicked(string trackId)
        signal offPicked()
        signal delayStep(real delta)
        signal resetDelay()

        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: menu.icon
            active: menu.panelOpen || menu.active
            tooltip: menu.title
            onClicked: {
                var wasOpen = menu.panelOpen
                root.closeMenus()
                menu.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Rectangle {
            visible: menu.panelOpen
            z: 20
            width: menu.panelWidth
            height: menu.panelHeight
            x: parent.width - width
            y: -height - 12
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                id: menuTitle
                x: 18
                y: 15
                text: menu.title
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: menuTitle.right
                anchors.leftMargin: 8
                anchors.verticalCenter: menuTitle.verticalCenter
                text: menu.count
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
            }
            Rectangle { x: 0; y: 48; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

            Rectangle {
                visible: menu.offRow
                x: 10
                y: 58
                width: parent.width - 20
                height: 34
                radius: 7
                color: !menu.active ? Qt.rgba(1, 1, 1, 0.10) : (offMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                border.width: !menu.active ? 1 : 0
                border.color: Qt.rgba(1, 1, 1, 0.10)
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Off"
                    color: !menu.active ? theme.ink : theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: offMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.offPicked()
                        menu.panelOpen = false
                    }
                }
            }

            ListView {
                id: menuList
                x: 10
                y: menu.offRow ? 98 : 58
                width: parent.width - 20
                height: parent.height - y - 46
                model: menu.delegateModel
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: trackRow
                    required property var modelData
                    width: ListView.view.width
                    height: 46
                    radius: 8
                    property bool selected: modelData.selected === true
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (trackMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: selected ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    Rectangle {
                        x: 10
                        y: 15
                        width: 16
                        height: 16
                        radius: 8
                        color: trackRow.selected ? theme.gold : Qt.rgba(1, 1, 1, 0.08)
                    }
                    Text {
                        x: 36
                        y: 7
                        width: parent.width - 48
                        text: root.trackTitle(modelData, menu.title === "Audio" ? "Audio track" : "Subtitle")
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 36
                        y: 25
                        width: parent.width - 48
                        text: root.trackMeta(modelData)
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: trackMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            menu.trackPicked("" + modelData.id)
                            menu.panelOpen = false
                        }
                    }
                }
            }

            Text {
                visible: menu.count === 0
                x: 18
                y: menu.offRow ? 108 : 68
                width: parent.width - 36
                text: menu.emptyText
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                height: 40
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "SYNC"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                DelayButton { text: "-0.1"; onClicked: menu.delayStep(-0.1) }
                Text {
                    width: 88
                    anchors.verticalCenter: parent.verticalCenter
                    text: (menu.syncValue >= 0 ? "+" : "") + menu.syncValue.toFixed(2) + "s"
                    color: theme.ink
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                }
                DelayButton { text: "+0.1"; onClicked: menu.delayStep(0.1) }
                DelayButton {
                    visible: Math.abs(menu.syncValue) > 0.0001
                    text: "0"
                    onClicked: menu.resetDelay()
                }
            }
        }
    }

    component SpeedMenuButton: Item {
        id: sm
        property bool panelOpen: false
        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: "speed"
            label: mpv.speed.toFixed(mpv.speed === Math.round(mpv.speed) ? 0 : 2) + "x"
            active: sm.panelOpen || Math.abs(mpv.speed - 1) > 0.001
            tooltip: "Speed"
            onClicked: {
                var wasOpen = sm.panelOpen
                root.closeMenus()
                sm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Rectangle {
            visible: sm.panelOpen
            z: 20
            width: 176
            height: 56 + root.speedChoices.length * 34
            x: parent.width - width
            y: -height - 12
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
            Text {
                x: 18
                y: 15
                text: "Speed"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Repeater {
                model: root.speedChoices
                delegate: Rectangle {
                    required property int index
                    required property real modelData
                    x: 8
                    y: 48 + index * 34
                    width: parent.width - 16
                    height: 32
                    radius: 8
                    property bool selected: Math.abs(mpv.speed - modelData) < 0.001
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (speedMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    Text {
                        anchors.centerIn: parent
                        text: modelData.toFixed(modelData === Math.round(modelData) ? 0 : 2) + "x"
                        color: parent.selected ? theme.gold : theme.ink
                        font.family: "Consolas"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: speedMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            mpv.speed = modelData
                            sm.panelOpen = false
                            root.wakeChrome()
                        }
                    }
                }
            }
        }
    }

    component FillMenuButton: Item {
        id: fm
        property bool panelOpen: false
        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: "fit"
            active: fm.panelOpen || root.fillModeIndex !== 0
            tooltip: "Video fill"
            onClicked: {
                var wasOpen = fm.panelOpen
                root.closeMenus()
                fm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Rectangle {
            visible: fm.panelOpen
            z: 20
            width: 188
            height: 56 + root.fillModes.length * 34
            x: parent.width - width
            y: -height - 12
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
            Text {
                x: 18
                y: 15
                text: "Video"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Repeater {
                model: root.fillModes
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    x: 8
                    y: 48 + index * 34
                    width: parent.width - 16
                    height: 32
                    radius: 8
                    property bool selected: root.fillModeIndex === index
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (fillMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.selected ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: fillMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.applyFill(index)
                    }
                }
            }
        }
    }

    component DelayButton: Item {
        id: db
        property string text: ""
        signal clicked()
        width: 42
        height: 24
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: dbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        }
        Text {
            anchors.centerIn: parent
            text: db.text
            color: theme.inkDim
            font.family: "Consolas"
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: dbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: db.clicked()
        }
    }

    component IconGlyph: Canvas {
        id: glyph
        property string kind: ""
        property string label: ""
        property bool hero: false
        property color ink: theme.ink
        antialiasing: true
        onKindChanged: requestPaint()
        onLabelChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var s = Math.min(w, h)
            var cx = w / 2
            var cy = h / 2
            ctx.clearRect(0, 0, w, h)
            ctx.strokeStyle = ink
            ctx.fillStyle = ink
            ctx.lineWidth = Math.max(1.5, s / 24)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            function line(x1, y1, x2, y2) {
                ctx.beginPath()
                ctx.moveTo(cx + x1 * s, cy + y1 * s)
                ctx.lineTo(cx + x2 * s, cy + y2 * s)
                ctx.stroke()
            }
            function circleArc(r, a1, a2, ccw) {
                ctx.beginPath()
                ctx.arc(cx, cy, r * s, a1 * Math.PI / 180, a2 * Math.PI / 180, ccw)
                ctx.stroke()
            }

            if (kind === "play") {
                ctx.beginPath()
                ctx.moveTo(cx - 0.08 * s, cy - 0.18 * s)
                ctx.lineTo(cx - 0.08 * s, cy + 0.18 * s)
                ctx.lineTo(cx + 0.21 * s, cy)
                ctx.closePath()
                ctx.fill()
            } else if (kind === "pause") {
                ctx.fillRect(cx - 0.16 * s, cy - 0.19 * s, 0.09 * s, 0.38 * s)
                ctx.fillRect(cx + 0.07 * s, cy - 0.19 * s, 0.09 * s, 0.38 * s)
            } else if (kind === "back") {
                line(0.12, -0.22, -0.12, 0)
                line(-0.12, 0, 0.12, 0.22)
            } else if (kind === "seekBack" || kind === "seekForward") {
                var fwd = kind === "seekForward"
                circleArc(0.27, fwd ? 320 : 220, fwd ? 55 : 140, !fwd)
                if (fwd) {
                    line(0.22, -0.23, 0.35, -0.20)
                    line(0.35, -0.20, 0.27, -0.08)
                } else {
                    line(-0.22, -0.23, -0.35, -0.20)
                    line(-0.35, -0.20, -0.27, -0.08)
                }
                ctx.font = "700 " + Math.round(s * 0.18) + "px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(label, cx, cy + s * 0.02)
            } else if (kind === "volume" || kind === "mute") {
                ctx.beginPath()
                ctx.moveTo(cx - 0.34 * s, cy - 0.10 * s)
                ctx.lineTo(cx - 0.20 * s, cy - 0.10 * s)
                ctx.lineTo(cx - 0.03 * s, cy - 0.25 * s)
                ctx.lineTo(cx - 0.03 * s, cy + 0.25 * s)
                ctx.lineTo(cx - 0.20 * s, cy + 0.10 * s)
                ctx.lineTo(cx - 0.34 * s, cy + 0.10 * s)
                ctx.closePath()
                ctx.stroke()
                if (kind === "mute") {
                    line(0.15, -0.14, 0.36, 0.14)
                    line(0.36, -0.14, 0.15, 0.14)
                } else {
                    ctx.beginPath()
                    ctx.arc(cx + 0.04 * s, cy, 0.22 * s, -0.7, 0.7)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(cx + 0.04 * s, cy, 0.34 * s, -0.62, 0.62)
                    ctx.stroke()
                }
            } else if (kind === "audio") {
                circleArc(0.20, 0, 360, false)
                line(-0.20, -0.02, -0.36, -0.16)
                line(0.20, -0.02, 0.36, -0.16)
                line(-0.12, 0.18, -0.22, 0.34)
                line(0.12, 0.18, 0.22, 0.34)
            } else if (kind === "subtitle") {
                ctx.strokeRect(cx - 0.30 * s, cy - 0.20 * s, 0.60 * s, 0.40 * s)
                line(-0.20, 0.02, -0.02, 0.02)
                line(0.08, 0.02, 0.22, 0.02)
                line(-0.20, 0.13, 0.20, 0.13)
            } else if (kind === "speed") {
                circleArc(0.30, 205, 335, false)
                line(0, 0, 0.18, -0.13)
                ctx.font = "700 " + Math.round(s * 0.17) + "px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(label || "1x", cx, cy + s * 0.22)
            } else if (kind === "fit") {
                ctx.strokeRect(cx - 0.27 * s, cy - 0.18 * s, 0.54 * s, 0.36 * s)
                line(-0.17, -0.08, -0.27, -0.18)
                line(0.17, 0.08, 0.27, 0.18)
            } else if (kind === "fullscreen") {
                line(-0.30, -0.12, -0.30, -0.30)
                line(-0.30, -0.30, -0.12, -0.30)
                line(0.30, -0.12, 0.30, -0.30)
                line(0.30, -0.30, 0.12, -0.30)
                line(-0.30, 0.12, -0.30, 0.30)
                line(-0.30, 0.30, -0.12, 0.30)
                line(0.30, 0.12, 0.30, 0.30)
                line(0.30, 0.30, 0.12, 0.30)
            }
        }
    }
}

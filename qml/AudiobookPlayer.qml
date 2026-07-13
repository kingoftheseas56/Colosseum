// AudiobookPlayer — the full-page REMOTE for the shared audiobook session. Owner: A2.
// No engine lives here anymore: audioSession (window root, Main.qml) owns the app's ONLY
// audiobook MpvItem; this page is chrome that binds to it — cover, chapter list, transport
// with speed + sleep timer. Opening, minimizing or closing this page never restarts the
// stream (Hemanth's ruling 2026-07-13: the player page and the reader strip are remotes).
//
// Chapters: a multi-file mp3 set → each FILE is a chapter; a single .m4b → mpv's embedded
// chapters. All of that (and Continue-resume) is the session's business now.

import QtQuick
import QtQuick.Controls

Item {
    id: player
    property Item backdrop
    readonly property var book: audioSession.book

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    Theme { id: theme }
    MouseArea { anchors.fill: parent }     // swallow clicks to the world beneath

    // solid dark ground (books/audio = page solid, frame OS)
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // ── remote glue: everything below delegates to the shared session ──
    function start(pk, b) { audioSession.openFor(pk, b) }
    function captureState() { return audioSession.captureState() }
    function restoreState(st) { audioSession.restoreState(st) }
    Component.onDestruction: if (audioSession) audioSession.recordProgress()

    // ── top bar ─────────────────────────────────────────────────────────────
    Glass {
        id: bar
        backdrop: player.backdrop
        x: theme.margin; y: 22
        width: player.width - theme.margin * 2
        height: 64; radius: 16
        Row {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter; spacing: 22
            BackAction {
                labelSize: 14; idleColor: theme.inkDim; hoverColor: theme.ink
                anchors.verticalCenter: parent.verticalCenter
                onTriggered: { audioSession.recordProgress(); player.backRequested() }
            }
            Text { text: "Listening"; color: theme.ink; font.family: theme.display; font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Repeater {
                model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]   // fullscreen-only: no maximize
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1,1,1,0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea { id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { audioSession.recordProgress()
                            if (modelData.a === "min") player.minimizeRequested(); else player.closeRequested() } }
                }
            }
        }
    }

    // ── body: cover (left) + chapter list (right) ─────────────────────────────
    Item {
        id: body
        anchors.top: bar.bottom; anchors.topMargin: 24
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.right: parent.right; anchors.rightMargin: theme.margin
        anchors.bottom: transport.top; anchors.bottomMargin: 20

        Column {
            id: coverCol
            width: 320
            anchors.top: parent.top; anchors.left: parent.left
            spacing: 22
            Item {
                width: 300; height: 300
                Rectangle { anchors.fill: parent; radius: 10; color: (player.book && player.book.c1) ? player.book.c1 : "#14131a" }
                Image {
                    anchors.fill: parent
                    source: (player.book && player.book.cover) ? player.book.cover : ""
                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                }
            }
            Text { width: 300; text: (player.book && player.book.title) ? player.book.title : ""
                color: theme.ink; font.family: theme.display; font.pixelSize: 26; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
            Text { width: 300; text: (player.book && player.book.author) ? player.book.author : ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14 }
        }

        // chapter list
        Column {
            anchors.top: parent.top; anchors.left: coverCol.right; anchors.leftMargin: 48
            anchors.right: parent.right; anchors.bottom: parent.bottom
            spacing: 12
            Text { text: (audioSession.multiFile ? "CHAPTERS  ·  " + audioSession.files.length : "CHAPTERS")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.6 }
            Flickable {
                id: chapScroll
                width: parent.width; height: parent.height - 40
                contentWidth: width; contentHeight: chapCol.implicitHeight
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: HouseScrollBar { flick: chapScroll }
                ScrollGlide { flick: chapScroll }
                Column {
                    id: chapCol
                    width: chapScroll.width
                    Repeater {
                        model: audioSession.chapterModel
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: chapCol.width; height: 46
                            property bool nowPlaying: audioSession.multiFile ? (index === audioSession.currentIndex)
                                                                             : (index === chapterIndexNow())
                            color: chMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                   : (nowPlaying ? Qt.rgba(0.94,0.77,0.29,0.07) : "transparent")
                            function chapterIndexNow() {
                                // for m4b: the chapter whose time <= position < next
                                var ch = audioSession.chapterModel, p = audioSession.position, r = 0
                                for (var i = 0; i < ch.length; i++) if (p >= ch[i].time) r = i
                                return r
                            }
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 14; anchors.right: parent.right; anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                text: (index + 1) + ".  " + modelData.label
                                color: parent.nowPlaying ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight; maximumLineCount: 1
                            }
                            Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.05) }
                            MouseArea { id: chMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.kind === "file") audioSession.playIndex(modelData.index)
                                    else { audioSession.seekTo(modelData.time); audioSession.paused = false }
                                } }
                        }
                    }
                }
            }
        }
    }

    // ── transport bar ─────────────────────────────────────────────────────────
    function fmt(t) {
        t = Math.max(0, Math.floor(t || 0))
        var h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = t % 60
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return (h > 0 ? h + ":" + pad(m) : m) + ":" + pad(s)
    }
    Glass {
        id: transport
        backdrop: player.backdrop
        x: theme.margin; anchors.bottom: parent.bottom; anchors.bottomMargin: 22
        width: player.width - theme.margin * 2; height: 96; radius: 16
        MouseArea { anchors.fill: parent }   // click-swallower

        // seek row
        Row {
            id: seekRow
            anchors.left: parent.left; anchors.leftMargin: 22; anchors.right: parent.right; anchors.rightMargin: 22
            anchors.top: parent.top; anchors.topMargin: 16; spacing: 14
            Text { text: player.fmt(audioSession.position); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter; width: 46; horizontalAlignment: Text.AlignRight }
            Item {
                width: seekRow.width - 46 - 46 - 28; height: 20
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 4; radius: 2; color: Qt.rgba(1,1,1,0.12) }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; height: 4; radius: 2; color: theme.gold
                    width: parent.width * (audioSession.duration > 0 ? audioSession.position / audioSession.duration : 0) }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: function(e) { if (audioSession.duration > 0) audioSession.seekTo(audioSession.duration * (e.x / width)) } }
            }
            Text { text: player.fmt(audioSession.duration); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter; width: 46 }
        }

        // control row
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 14; spacing: 26

            // -30s
            AbTransportButton { glyph: "«"; sub: "30"; onTapped: audioSession.seekRel(-30) }
            // play/pause
            Rectangle {
                width: 48; height: 48; radius: 24; color: theme.gold
                anchors.verticalCenter: parent.verticalCenter
                Text { anchors.centerIn: parent; text: audioSession.paused ? "▶" : "❚❚"; color: "#241a05"; font.pixelSize: audioSession.paused ? 18 : 15 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: audioSession.togglePlay() }
            }
            // +30s
            AbTransportButton { glyph: "»"; sub: "30"; onTapped: audioSession.seekRel(30) }
        }

        // right cluster: speed + sleep
        Row {
            anchors.right: parent.right; anchors.rightMargin: 22
            anchors.bottom: parent.bottom; anchors.bottomMargin: 18; spacing: 10
            // speed
            Rectangle {
                width: speedT.implicitWidth + 22; height: 30; radius: 15
                color: speedMa.containsMouse ? Qt.rgba(1,1,1,0.12) : Qt.rgba(1,1,1,0.06)
                border.width: 1; border.color: theme.edge
                property var speeds: [1.0, 1.25, 1.5, 1.75, 2.0, 0.75]
                Text { id: speedT; anchors.centerIn: parent; text: audioSession.speed.toFixed(2).replace(/0$/,"") + "×"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { id: speedMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var s = parent.speeds, cur = audioSession.speed, next = s[0]
                        for (var i = 0; i < s.length; i++) if (Math.abs(s[i] - cur) < 0.01) { next = s[(i + 1) % s.length]; break }
                        audioSession.setRate(next)
                    } }
            }
            // sleep timer (state lives in the session, so it survives minimize)
            Rectangle {
                width: sleepT.implicitWidth + 22; height: 30; radius: 15
                color: sleepMa.containsMouse ? Qt.rgba(1,1,1,0.12) : (audioSession.sleepMinutes > 0 ? Qt.rgba(0.94,0.77,0.29,0.10) : Qt.rgba(1,1,1,0.06))
                border.width: 1; border.color: audioSession.sleepMinutes > 0 ? Qt.rgba(0.94,0.77,0.29,0.4) : theme.edge
                property var steps: [0, 15, 30, 45, 60]
                Text { id: sleepT; anchors.centerIn: parent
                    text: audioSession.sleepMinutes > 0 ? ("☾ " + audioSession.sleepMinutes + "m") : "☾ Sleep"
                    color: audioSession.sleepMinutes > 0 ? theme.gold : theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                MouseArea { id: sleepMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var s = parent.steps, cur = audioSession.sleepMinutes, next = s[0]
                        for (var i = 0; i < s.length; i++) if (s[i] === cur) { next = s[(i + 1) % s.length]; break }
                        audioSession.sleepMinutes = next
                    } }
            }
        }
    }

    // empty state (files vanished)
    Text {
        visible: !audioSession.ready
        anchors.centerIn: parent
        text: "This audiobook isn't downloaded."
        color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 20
    }
}

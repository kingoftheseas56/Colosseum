// AudiobookPlayer — the full-session audiobook listening surface. Owner: A2.
// Audio-shaped chrome on the shared libmpv backend (no video surface): a large cover,
// a chapter list, a transport bar with speed + sleep timer, and session hooks so it
// rides the taskbar/Continue system like a movie. Fed by the downloaded files of a
// paired audiobook (Audiobooks.localFiles(pairKey)).
//
// Chapters: a multi-file mp3 set → each FILE is a chapter (advance on endFile); a single
// .m4b → mpv's embedded `chapters`. captureState/restoreState carry {fileIndex, position}
// for minimize + Continue resume (Option A — listening position is separate from reading).

import QtQuick
import QtQuick.Controls
import Colosseum.Player

Item {
    id: player
    property Item backdrop
    property var book: ({})
    property string pairKey: ""
    property var files: []                 // ordered local audio file paths
    property int currentIndex: 0
    property bool ready: false
    // A resume position parked until the file finishes loading. A seekExact issued
    // before mpv has the file open is a no-op, so cold-resume must wait for onFileLoaded.
    property real pendingResumeSec: -1

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

    // ── the engine (audio only; the mpv surface is never shown) ──
    MpvItem { id: mpv; width: 1; height: 1; visible: false }

    // chapter model: mp3 set → files; single m4b → mpv chapters
    readonly property bool multiFile: player.files.length > 1
    property var chapterModel: []
    function rebuildChapters() {
        var out = []
        if (player.multiFile) {
            for (var i = 0; i < player.files.length; i++) {
                var base = String(player.files[i]).split(/[\\/]/).pop().replace(/\.[^.]+$/, "")
                out.push({ label: base, kind: "file", index: i, time: 0 })
            }
        } else {
            var ch = mpv.chapters || []
            for (var j = 0; j < ch.length; j++)
                out.push({ label: (ch[j].title || ("Chapter " + (j + 1))), kind: "seek", index: j,
                           time: Number(ch[j].time) || 0 })
            if (out.length === 0 && player.files.length === 1)
                out.push({ label: (player.book && player.book.title) ? player.book.title : "Audiobook",
                           kind: "seek", index: 0, time: 0 })
        }
        player.chapterModel = out
    }
    Connections { target: mpv; function onChaptersChanged() { if (!player.multiFile) player.rebuildChapters() } }

    // ── load / playlist advance ──
    function start(pk, b) {
        player.pairKey = pk
        player.book = b || ({})
        player.files = (typeof Audiobooks !== 'undefined') ? Audiobooks.localFiles(pk) : []
        player.currentIndex = 0
        player.ready = player.files.length > 0
        player.rebuildChapters()
        player.pendingResumeSec = -1   // fresh start; restoreState parks a resume point if the session had one
        if (player.ready) {
            mpv.loadFile(player.files[0])
            // resume position rides in via restoreState after load if the session had one
        }
    }
    function playIndex(i) {
        if (i < 0 || i >= player.files.length) return
        player.pendingResumeSec = -1   // a deliberate jump cancels any parked resume seek
        player.currentIndex = i
        mpv.loadFile(player.files[i])
        mpv.pause = false
    }
    // a file ended → advance to the next in a multi-file set
    Connections {
        target: mpv
        function onEndFile(reason) {
            if (reason === "eof" && player.multiFile && player.currentIndex + 1 < player.files.length)
                player.playIndex(player.currentIndex + 1)
        }
        function onFileLoaded() {
            // A resume seek parked before the file was open lands here (a pre-load seekExact
            // no-ops). Apply it, then skip this record — the seek resolves async, so recording
            // now would still stamp ~0 over the saved spot; the 10s timer records the true one.
            if (player.pendingResumeSec > 0) {
                mpv.seekExact(player.pendingResumeSec)
                player.pendingResumeSec = -1
                return
            }
            player.recordProgress()
        }
    }

    // ── session state (minimize + Continue resume) ──
    function captureState() {
        return { "fileIndex": player.currentIndex, "position": mpv.position }
    }
    function restoreState(st) {
        if (!st) return
        var idx = Number(st.fileIndex) || 0
        if (idx !== player.currentIndex && idx < player.files.length) player.playIndex(idx)
        var pos = Number(st.position) || 0
        // If the file is already open (minimize/restore of the same file), seek live;
        // otherwise park it for onFileLoaded (a pre-load seek would silently no-op).
        if (pos > 0 && mpv.duration > 0) { mpv.seekExact(pos); player.pendingResumeSec = -1 }
        else { player.pendingResumeSec = (pos > 0) ? pos : -1 }
    }

    // ── Continue/resume: record listening position (never a movie-style 90% auto-drop) ──
    function overallProgress() {
        // coarse: (completed files + current fraction) / file count — good enough for the row bar
        if (player.files.length === 0) return 0
        var frac = (mpv.duration > 0) ? (mpv.position / mpv.duration) : 0
        return (player.currentIndex + frac) / player.files.length
    }
    function recordProgress() {
        if (player.pendingResumeSec > 0) return   // resume not applied yet — the store's saved spot is still the truth
        if (!player.ready || typeof Progress === 'undefined' || !player.pairKey) return
        Progress.record({
            "kind": "audiobook", "id": player.pairKey,
            "caption": (player.book && player.book.title) ? player.book.title : "Audiobook",
            "title": (player.book && player.book.title) ? player.book.title : "Audiobook",
            "sub": (player.book && player.book.author) ? player.book.author : "",
            "cover": (player.book && player.book.cover) ? player.book.cover : "",
            "progress": player.overallProgress(),
            "resume": { "pairKey": player.pairKey, "fileIndex": player.currentIndex,
                        "position": mpv.position, "book": player.book }
        })
    }
    Timer { interval: 10000; running: player.ready; repeat: true; onTriggered: player.recordProgress() }
    Component.onDestruction: player.recordProgress()

    // ── sleep timer ──
    property int sleepMinutes: 0            // 0 = off
    Timer {
        id: sleepTimer
        interval: player.sleepMinutes * 60000
        running: player.sleepMinutes > 0
        onTriggered: { mpv.pause = true; player.sleepMinutes = 0 }
    }

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
                onTriggered: { player.recordProgress(); player.backRequested() }
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
                        onClicked: { player.recordProgress()
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
            Text { text: (player.multiFile ? "CHAPTERS  ·  " + player.files.length : "CHAPTERS")
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
                        model: player.chapterModel
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: chapCol.width; height: 46
                            property bool nowPlaying: player.multiFile ? (index === player.currentIndex)
                                                                       : (mpv.chapters && index === chapterIndexNow())
                            color: chMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                   : (nowPlaying ? Qt.rgba(0.94,0.77,0.29,0.07) : "transparent")
                            function chapterIndexNow() {
                                // for m4b: the chapter whose time <= position < next
                                var ch = player.chapterModel, p = mpv.position, r = 0
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
                                    if (modelData.kind === "file") player.playIndex(modelData.index)
                                    else { mpv.seekExact(modelData.time); mpv.pause = false }
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
            Text { text: player.fmt(mpv.position); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter; width: 46; horizontalAlignment: Text.AlignRight }
            Item {
                width: seekRow.width - 46 - 46 - 28; height: 20
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 4; radius: 2; color: Qt.rgba(1,1,1,0.12) }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; height: 4; radius: 2; color: theme.gold
                    width: parent.width * (mpv.duration > 0 ? mpv.position / mpv.duration : 0) }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: function(e) { if (mpv.duration > 0) mpv.seekExact(mpv.duration * (e.x / width)) } }
            }
            Text { text: player.fmt(mpv.duration); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter; width: 46 }
        }

        // control row
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 14; spacing: 26

            // -30s
            AbTransportButton { glyph: "«"; sub: "30"; onTapped: mpv.seekExact(Math.max(0, mpv.position - 30)) }
            // play/pause
            Rectangle {
                width: 48; height: 48; radius: 24; color: theme.gold
                anchors.verticalCenter: parent.verticalCenter
                Text { anchors.centerIn: parent; text: mpv.pause ? "▶" : "❚❚"; color: "#241a05"; font.pixelSize: mpv.pause ? 18 : 15 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { mpv.pause = !mpv.pause; player.recordProgress() } }
            }
            // +30s
            AbTransportButton { glyph: "»"; sub: "30"; onTapped: mpv.seekExact(Math.min(mpv.duration, mpv.position + 30)) }
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
                Text { id: speedT; anchors.centerIn: parent; text: mpv.speed.toFixed(2).replace(/0$/,"") + "×"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { id: speedMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var s = parent.speeds, cur = mpv.speed, next = s[0]
                        for (var i = 0; i < s.length; i++) if (Math.abs(s[i] - cur) < 0.01) { next = s[(i + 1) % s.length]; break }
                        mpv.speed = next
                    } }
            }
            // sleep timer
            Rectangle {
                width: sleepT.implicitWidth + 22; height: 30; radius: 15
                color: sleepMa.containsMouse ? Qt.rgba(1,1,1,0.12) : (player.sleepMinutes > 0 ? Qt.rgba(0.94,0.77,0.29,0.10) : Qt.rgba(1,1,1,0.06))
                border.width: 1; border.color: player.sleepMinutes > 0 ? Qt.rgba(0.94,0.77,0.29,0.4) : theme.edge
                property var steps: [0, 15, 30, 45, 60]
                Text { id: sleepT; anchors.centerIn: parent
                    text: player.sleepMinutes > 0 ? ("☾ " + player.sleepMinutes + "m") : "☾ Sleep"
                    color: player.sleepMinutes > 0 ? theme.gold : theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                MouseArea { id: sleepMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var s = parent.steps, cur = player.sleepMinutes, next = s[0]
                        for (var i = 0; i < s.length; i++) if (s[i] === cur) { next = s[(i + 1) % s.length]; break }
                        player.sleepMinutes = next
                    } }
            }
        }
    }

    // empty state (files vanished)
    Text {
        visible: !player.ready
        anchors.centerIn: parent
        text: "This audiobook isn't downloaded."
        color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 20
    }
}

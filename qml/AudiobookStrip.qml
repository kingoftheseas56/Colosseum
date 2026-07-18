// AudiobookStrip — the reader's docked listen strip: the SECOND remote onto the ONE
// shared audiobook engine (AudiobookSession, window root in Main.qml). Owner: A2.
//
// A pure REMOTE: no MpvItem, no playback state of its own — every control and every
// label delegates to the injected `session`. Contract (Hemanth 2026-07-13, one engine
// many faces):
//   - openFor() ATTACHES idempotently: if the session already plays this pairKey the
//     stream is untouched — the strip and the full player page show one truth.
//   - hide()/close NEVER stops the session: the stream may play on behind the reader
//     and in the full player page. The full player (or a real session close in
//     Main.qml) is the only place a stream actually dies.
//   - If another book takes the engine (activePairKey changes under us), the strip
//     HIDES rather than re-labeling — it was summoned for THIS book, and showing the
//     other book's transport inside this book's reader would lie about what the pill
//     button opened.
import QtQuick

Item {
    id: strip

    // the shared AudiobookSession, injected by the host (the reader shell binds audioSession)
    property var session: null
    // the book this strip was summoned FOR (visibility is keyed to it — see header)
    property string boundPairKey: ""
    property bool summoned: false

    readonly property bool live: strip.summoned && !!strip.session && strip.session.ready
                                 && strip.session.activePairKey === strip.boundPairKey

    visible: strip.live || toast.visible
    height: 96          // panel + breathing room; the host anchors left/right/bottom
    Theme { id: theme }

    // Summon the strip for a book. Attach-only: the session decides whether this is
    // a no-op (same book already streaming) or a fresh load with Continue-resume.
    function openFor(pk, book) {
        if (!strip.session || !pk) { toast.show("No audiobook downloaded"); return }
        // gate BEFORE touching the engine: openFor() on an undownloaded key would
        // retarget the session away from a live stream and strand it remoteless
        if (typeof Audiobooks !== 'undefined' && !Audiobooks.isDownloaded(pk)) {
            toast.show("No audiobook downloaded")
            return
        }
        strip.session.openFor(pk, book || ({}))
        if (!strip.session.ready) { toast.show("No audiobook downloaded"); return }
        strip.boundPairKey = pk
        strip.summoned = true
    }

    // Read-along summon: like openFor, but the fresh open lands PAUSED at the exact last
    // spot (the reader auto-summons this when you open a paired book — you press play to
    // start reading along). Idempotent: if the session already streams this pairKey the
    // stream is untouched (openFor no-ops), and we just (re)bind the strip so it shows.
    function summonPaused(pk) {
        if (!strip.session || !pk) { toast.show("No audiobook downloaded"); return }
        if (typeof Audiobooks !== 'undefined' && !Audiobooks.isDownloaded(pk)) {
            toast.show("No audiobook downloaded")
            return
        }
        strip.session.openFor(pk, ({}), true)   // paused at last spot; no-op if already live
        if (!strip.session.ready) { toast.show("No audiobook downloaded"); return }
        strip.boundPairKey = pk
        strip.summoned = true
    }

    // Drop the strip WITHOUT stopping the session — the stream may play on elsewhere
    // (that's the whole remote contract; stopping is the full player's job).
    function hide() { strip.summoned = false }

    // Leaving the reader ends the read-along companion. When this strip is torn down
    // (the reader was exited) while it was still actively summoned for THIS book, stop
    // the stream — otherwise the audiobook plays on with no visible player (Hemanth
    // 2026-07-15). This refines the 2026-07-13 remote contract for the read-along entry
    // point: an explicit hide() sets summoned=false first, so "keep playing behind the
    // reader" still survives; only a live companion you walk out of is stopped. The
    // activePairKey guard means we never stop a stream another book has taken over.
    Component.onDestruction: {
        if (strip.summoned && strip.session && strip.session.ready
            && strip.session.activePairKey === strip.boundPairKey)
            strip.session.stop()
    }

    // TTS mutual exclusion (one engine per ear): the HTML TTS strip is opening, so
    // pause OUR stream and get out of its way (both strips dock to the same bottom
    // edge). Pause, not stop — the spot is kept and the session survives.
    function pauseForTts() {
        if (strip.session && strip.session.ready && !strip.session.paused)
            strip.session.togglePlay()          // togglePlay also records the Continue spot
        strip.summoned = false
    }

    function fmt(t) {
        t = Math.max(0, Math.floor(t || 0))
        var h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = t % 60
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return (h > 0 ? h + ":" + pad(m) : m) + ":" + pad(s)
    }

    // ── the docked panel ────────────────────────────────────────────────────────
    // Solid dark panel, not Glass: the reader has no wallpaper backdrop item to blur
    // (Glass requires one), and books/audio doctrine is "page solid, frame OS".
    Rectangle {
        id: panel
        visible: strip.live
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 18; anchors.rightMargin: 18; anchors.bottomMargin: 14
        height: 72; radius: 16
        color: Qt.rgba(0.055, 0.06, 0.09, 0.97)
        border.width: 1; border.color: theme.edge

        MouseArea { anchors.fill: parent }   // click-swallower (house rule for floating panels)

        // thin gold progress line along the panel's top edge
        Item {
            anchors.top: parent.top; anchors.topMargin: 0
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 16; anchors.rightMargin: 16
            height: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width; height: 2; radius: 1; color: Qt.rgba(1, 1, 1, 0.10)
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: 2; radius: 1; color: theme.gold
                width: parent.width * ((strip.session && strip.session.duration > 0)
                                       ? strip.session.position / strip.session.duration : 0)
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: function (e) {
                    if (strip.session && strip.session.duration > 0)
                        strip.session.seekTo(strip.session.duration * (e.x / width))
                }
            }
        }

        // left: title · position / duration
        Column {
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 4
            spacing: 2
            width: Math.min(360, panel.width * 0.34)
            Text {
                width: parent.width
                text: (strip.session && strip.session.book && strip.session.book.title)
                      ? strip.session.book.title : "Audiobook"
                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                font.weight: Font.DemiBold; elide: Text.ElideRight; maximumLineCount: 1
            }
            Text {
                width: parent.width
                text: strip.session
                      ? (strip.fmt(strip.session.position) + " / " + strip.fmt(strip.session.duration)
                         + (strip.session.multiFile
                            ? ("  ·  " + (strip.session.currentIndex + 1) + " of " + strip.session.files.length)
                            : ""))
                      : ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                elide: Text.ElideRight; maximumLineCount: 1
            }
        }

        // center: −30 · play/pause · +30 (the session's real transport verbs)
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 4
            spacing: 20
            AbTransportButton {
                glyph: "«"; sub: "30"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: if (strip.session) strip.session.seekRel(-30)
            }
            Rectangle {
                width: 42; height: 42; radius: 21; color: theme.gold
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.centerIn: parent
                    text: (strip.session && strip.session.paused) ? "▶" : "❚❚"
                    color: "#241a05"
                    font.pixelSize: (strip.session && strip.session.paused) ? 16 : 13
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: if (strip.session) strip.session.togglePlay()
                }
            }
            AbTransportButton {
                glyph: "»"; sub: "30"
                anchors.verticalCenter: parent.verticalCenter
                onTapped: if (strip.session) strip.session.seekRel(30)
            }
        }

        // right: speed pill + close (close = hide; the stream plays on)
        Row {
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 4
            spacing: 10
            Rectangle {
                width: speedT.implicitWidth + 22; height: 30; radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: speedMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: theme.edge
                property var speeds: [1.0, 1.25, 1.5, 1.75, 2.0, 0.75]   // AudiobookPlayer's cycle
                Text {
                    id: speedT; anchors.centerIn: parent
                    text: (strip.session ? strip.session.speed : 1.0).toFixed(2).replace(/0$/, "") + "×"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
                MouseArea {
                    id: speedMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!strip.session) return
                        var s = parent.speeds, cur = strip.session.speed, next = s[0]
                        for (var i = 0; i < s.length; i++)
                            if (Math.abs(s[i] - cur) < 0.01) { next = s[(i + 1) % s.length]; break }
                        strip.session.setRate(next)
                    }
                }
            }
            Rectangle {
                width: 30; height: 30; radius: 8
                anchors.verticalCenter: parent.verticalCenter
                color: closeMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: theme.inkDimmer; font.pixelSize: 12 }
                MouseArea {
                    id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: strip.hide()   // hide, never stop — the full player owns stopping
                }
            }
        }
    }

    // ── honest empty answer: a transient toast, never a fake strip ──────────────
    Rectangle {
        id: toast
        visible: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        width: toastT.implicitWidth + 36; height: 38; radius: 19
        color: Qt.rgba(0.055, 0.06, 0.09, 0.97)
        border.width: 1; border.color: theme.edge
        function show(msg) { toastT.text = msg; toast.visible = true; toastTimer.restart() }
        Text {
            id: toastT; anchors.centerIn: parent; text: ""
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
        }
        Timer { id: toastTimer; interval: 2600; onTriggered: toast.visible = false }
    }
}

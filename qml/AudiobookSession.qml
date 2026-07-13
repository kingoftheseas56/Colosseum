// AudiobookSession — the ONE audiobook playback engine, app-wide. Owner: A2.
// Lives at the window root in Main.qml (never inside a Loader), so the stream survives
// every surface change: the full player page (AudiobookPlayer.qml) and the reader's
// listening strip are both REMOTES that bind to this session — one engine, many faces
// (Hemanth's ruling 2026-07-13: the strip is a remote, not a second player).
//
// The brain is lifted from the shipped AudiobookPlayer.qml: a multi-file mp3 set makes
// each FILE a chapter (advance on endFile); a single .m4b uses mpv's embedded `chapters`.
// openFor() is IDEMPOTENT — re-opening the live pairKey keeps the stream and position
// untouched (start listening in the reader, close the book, open the full player: same
// stream, nothing restarts). A different pairKey loads fresh and resumes from the
// Continue-saved spot (Progress.get("audiobook", pk).resume — the 10s heartbeat below
// keeps that spot current, so it is always a good resume point).

import QtQuick
import Colosseum.Player

Item {
    id: session
    visible: false
    width: 0; height: 0

    property string activePairKey: ""
    property var book: ({})
    property var files: []                 // ordered local audio file paths
    property int currentIndex: 0
    property bool ready: false
    readonly property bool multiFile: session.files.length > 1
    property var chapterModel: []

    // transport surface for the remotes (aliases ride mpv's own notify signals)
    property alias position: mpv.position
    property alias duration: mpv.duration
    property alias paused: mpv.pause
    property alias speed: mpv.speed

    // resume precision rides the load: a seek issued before the file is open NO-OPS
    // (PlayerPage's pendingSeekSec lesson) — park it here, apply in onFileLoaded.
    property real pendingResumeSec: -1

    // ── the engine (audio only; the mpv surface is never shown — cover art is the remotes' job) ──
    MpvItem { id: mpv; width: 1; height: 1; visible: false }

    // chapter model: mp3 set → files; single m4b → mpv chapters
    function rebuildChapters() {
        var out = []
        if (session.multiFile) {
            for (var i = 0; i < session.files.length; i++) {
                var base = String(session.files[i]).split(/[\\/]/).pop().replace(/\.[^.]+$/, "")
                out.push({ label: base, kind: "file", index: i, time: 0 })
            }
        } else {
            var ch = mpv.chapters || []
            for (var j = 0; j < ch.length; j++)
                out.push({ label: (ch[j].title || ("Chapter " + (j + 1))), kind: "seek", index: j,
                           time: Number(ch[j].time) || 0 })
            if (out.length === 0 && session.files.length === 1)
                out.push({ label: (session.book && session.book.title) ? session.book.title : "Audiobook",
                           kind: "seek", index: 0, time: 0 })
        }
        session.chapterModel = out
    }
    Connections { target: mpv; function onChaptersChanged() { if (!session.multiFile) session.rebuildChapters() } }

    // ── open / load — THE HEART ──
    // Same pairKey + ready → NO-OP (the stream keeps playing exactly where it is).
    // Different pairKey → save the outgoing book's spot, load fresh, resume from Continue.
    function openFor(pk, b) {
        if (!pk) return
        if (session.ready && session.activePairKey === pk) return
        if (session.ready) session.recordProgress()          // leaving the old book: save its spot first
        session.activePairKey = pk
        session.book = b || ({})
        session.files = (typeof Audiobooks !== 'undefined') ? Audiobooks.localFiles(pk) : []
        session.currentIndex = 0
        session.ready = session.files.length > 0
        session.rebuildChapters()
        if (!session.ready) return
        // Continue resume: the saved listening spot rides in from ProgressStore
        var idx = 0, pos = 0
        if (typeof Progress !== 'undefined') {
            var pg = Progress.get("audiobook", pk)
            if (pg && pg.resume) {
                idx = Number(pg.resume.fileIndex) || 0
                pos = Number(pg.resume.position) || 0
            }
        }
        if (idx < 0 || idx >= session.files.length) idx = 0
        session.currentIndex = idx
        session.pendingResumeSec = (pos > 0) ? pos : -1   // applied in onFileLoaded — a pre-load seek no-ops
        mpv.loadFile(session.files[idx])
        mpv.pause = false                     // a fresh open plays (parity with the old fresh-mpv surface)
    }
    function playIndex(i) {
        if (i < 0 || i >= session.files.length) return
        session.pendingResumeSec = -1         // a deliberate jump cancels any in-flight resume seek
        session.currentIndex = i
        mpv.loadFile(session.files[i])
        mpv.pause = false
    }
    function togglePlay() { mpv.pause = !mpv.pause; session.recordProgress() }
    function seekRel(delta) { mpv.seekExact(Math.max(0, Math.min(mpv.duration, mpv.position + delta))) }
    function seekTo(t) { mpv.seekExact(Math.max(0, t)) }
    function setRate(r) { mpv.speed = r }
    // a real close ends the stream for good (minimize never calls this — remotes just drop away)
    function stop() {
        if (session.ready) session.recordProgress()          // save the spot before the stream dies
        mpv.command(["stop"])
        session.pendingResumeSec = -1
        session.ready = false
        session.activePairKey = ""
        session.book = ({})
        session.files = []
        session.currentIndex = 0
        session.chapterModel = []
        session.sleepMinutes = 0
    }
    // a file ended → advance to the next in a multi-file set
    Connections {
        target: mpv
        function onEndFile(reason) {
            if (reason === "eof" && session.multiFile && session.currentIndex + 1 < session.files.length)
                session.playIndex(session.currentIndex + 1)
        }
        function onFileLoaded() {
            if (session.pendingResumeSec > 0) {
                // apply the parked resume seek — and SKIP this heartbeat: mpv.position is
                // still ~0 here (the seek lands async), so recording now would overwrite
                // the saved Continue spot with 0. The 10s Timer records the true spot.
                mpv.seekExact(session.pendingResumeSec)
                session.pendingResumeSec = -1
            } else {
                session.recordProgress()
            }
        }
    }

    // ── session state (kept for the Sessions record contract; {fileIndex, position}) ──
    function captureState() {
        return { "fileIndex": session.currentIndex, "position": mpv.position }
    }
    function restoreState(st) {
        if (!st) return
        var idx = Number(st.fileIndex) || 0
        var pos = Number(st.position) || 0
        if (idx !== session.currentIndex && idx >= 0 && idx < session.files.length) {
            session.playIndex(idx)                           // triggers a load (and clears stale pending)…
            if (pos > 0) session.pendingResumeSec = pos      // …so the seek rides its onFileLoaded
        } else if (pos > 0) {
            mpv.seekExact(pos)   // same file, already open — a live seek works (acceptResumeChoice precedent)
        }
    }

    // ── Continue/resume: record listening position (never a movie-style 90% auto-drop) ──
    function overallProgress() {
        // coarse: (completed files + current fraction) / file count — good enough for the row bar
        if (session.files.length === 0) return 0
        var frac = (mpv.duration > 0) ? (mpv.position / mpv.duration) : 0
        return (session.currentIndex + frac) / session.files.length
    }
    function recordProgress() {
        if (session.pendingResumeSec > 0) return   // resume not applied yet — the store's spot is still the truth
        if (!session.ready || typeof Progress === 'undefined' || !session.activePairKey) return
        Progress.record({
            "kind": "audiobook", "id": session.activePairKey,
            "caption": (session.book && session.book.title) ? session.book.title : "Audiobook",
            "title": (session.book && session.book.title) ? session.book.title : "Audiobook",
            "sub": (session.book && session.book.author) ? session.book.author : "",
            "cover": (session.book && session.book.cover) ? session.book.cover : "",
            "progress": session.overallProgress(),
            "resume": { "pairKey": session.activePairKey, "fileIndex": session.currentIndex,
                        "position": mpv.position, "book": session.book }
        })
    }
    Timer { interval: 10000; running: session.ready; repeat: true; onTriggered: session.recordProgress() }
    Component.onDestruction: session.recordProgress()

    // ── sleep timer (lives here so it survives the page being minimized) ──
    property int sleepMinutes: 0            // 0 = off
    Timer {
        interval: session.sleepMinutes * 60000
        running: session.sleepMinutes > 0
        onTriggered: { mpv.pause = true; session.sleepMinutes = 0 }
    }
}

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
import Colosseum.Activity
import "ActivityLaneHelpers.js" as ActivityLaneHelpers

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
    property alias volume: mpv.volume      // 0..100 linear (player-house rule, Hemanth 2026-07-09)
    property alias mute: mpv.mute

    // resume precision rides the load: a seek issued before the file is open NO-OPS
    // (PlayerPage's pendingSeekSec lesson) — park it here, apply in onFileLoaded.
    property real pendingResumeSec: -1

    // ── the engine (audio only; the mpv surface is never shown — cover art is the remotes' job) ──
    MpvItem { id: mpv; width: 1; height: 1; visible: false }

    // --- Your Colosseum activity (Lane E, CPP-PORT-CONTRACT.md §9): mirrors PlayerPage.qml's
    // Lane A pattern beside this app-wide session. activePairKey is BiblioApi.pairKey(title,
    // author) — a normalized text key (see BiblioApi.js), not a canonical cross-service book
    // ID, so it is a trustworthy LOCAL item identity but not verified portable proof of "the
    // same edition" across devices. Marked syncable:false and namespaced per §7 Audiobook /
    // §25 fail-closed ("unsafe/path identity -> local-only event"), same conservative
    // treatment Lane D already gives the identical pairKey concept for Biblio ebooks.
    property string activityActiveKey: ""
    ActivityPlaybackTracker {
        id: activityTracker
        // sink NEVER breaks playback if ProfileActivity is absent/unbound (§25) — this session
        // outlives every UI surface, so the guard must hold across profile switches too.
        sink: (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
    }
    // Begin session: called when a genuinely new pairKey becomes active (openFor's own
    // same-pairKey no-op guard already means this only runs for a real item change). Identity
    // derivation and the begin/no-op/end state-transition rule live in the shared
    // ActivityLaneHelpers.js module (also used by PlayerPage.qml's Lane A) and are covered
    // directly by tests/qml/tst_audiobook_activity.qml.
    function activityBeginIfNeeded() {
        var idf = ActivityLaneHelpers.audiobookIdentityFor(session.activePairKey)
        var action = ActivityLaneHelpers.decideTransition(session.activityActiveKey, idf)
        if (action === "noop")
            return
        session.activityEndSession()
        if (action === "end")
            return
        session.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
        var sink = (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
        var sessionId = (sink && sink.newSessionId) ? sink.newSessionId() : ""
        activityTracker.begin({
            "world": "biblio",
            "kind": "audiobook",
            "titleKey": idf.titleKey,
            "itemKey": idf.itemKey,
            "title": (session.book && session.book.title) ? session.book.title : "",
            "itemLabel": "",
            "cover": "",   // book.cover may be a local/relative asset locator — never portable (§15)
            "syncable": false,
            "source": "audiobook_session"
        }, sessionId)
    }
    // Sampling: the existing 10s Progress heartbeat Timer below also drives this — independent
    // cadence is fine (the tracker measures wall/media deltas, not ticks, §9 Lane E). consuming
    // is ready && !paused; the heartbeat's own `running: session.ready` condition (NOT gated on
    // paused — see AUDIT.md Lane 5) stays byte-identical, so a paused sample still arrives but
    // the tracker itself discards it via consuming:false.
    function activitySample() {
        if (!session.activityActiveKey.length) return
        var rateMilli = (mpv.speed && mpv.speed > 0) ? Math.round(mpv.speed * 1000) : 1000
        var consuming = session.ready && !mpv.pause
        activityTracker.sample(Math.round(mpv.position * 1000), Math.round(mpv.duration * 1000),
                                rateMilli, consuming)
    }
    // Discontinuity: explicit seek, saved-resume seek, file/chapter source switch, pause/sleep-
    // timer pause, speed change, abnormal load/recovery (§9 Lane E). `atSec`, when given, is the
    // authoritative post-jump position (e.g. a resume seek mpv.position has not caught up to
    // yet); otherwise the current mpv.position is used.
    function activityDiscontinuity(atSec) {
        if (!session.activityActiveKey.length) return
        var posSec = (atSec !== undefined) ? atSec : mpv.position
        var rateMilli = (mpv.speed && mpv.speed > 0) ? Math.round(mpv.speed * 1000) : 1000
        activityTracker.discontinuity(Math.round(posSec * 1000), Math.round(mpv.duration * 1000), rateMilli)
    }
    // Final-file EOF only (§9 Lane E: "intermediate multi-file EOF does not complete" — that
    // case is a plain activityDiscontinuity() from the file-switch, never this). Ends the
    // session too, so a later replay begins a fresh one rather than reusing this one.
    function activityNaturalEof() {
        if (!session.activityActiveKey.length) return
        activityTracker.naturalEof()
        activityTracker.endSession()
        session.activityActiveKey = ""
    }
    function activityEndSession() {
        if (!session.activityActiveKey.length) return
        activityTracker.endSession()
        session.activityActiveKey = ""
    }

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
    // startPaused (optional): true → load at the resumed spot but DON'T play. The reader's
    // read-along summon uses this — opening a paired book restores the exact last listening
    // spot, paused, waiting for you to press play (Hemanth 2026-07-15). Default false keeps
    // the full player + fresh opens playing, as before.
    function openFor(pk, b, startPaused) {
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
        session.activityBeginIfNeeded()   // Activity (Lane E): new pairKey — end old session, begin new one
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
        mpv.pause = (startPaused === true)    // summon-on-open loads paused at last spot; else plays
    }
    function playIndex(i) {
        if (i < 0 || i >= session.files.length) return
        session.pendingResumeSec = -1         // a deliberate jump cancels any in-flight resume seek
        session.currentIndex = i
        session.activityDiscontinuity(0)   // Activity (Lane E): file/chapter source switch
        mpv.loadFile(session.files[i])
        mpv.pause = false
    }
    function togglePlay() { mpv.pause = !mpv.pause; session.recordProgress() }
    // Deterministic play (read-along, Task 6): the ReadAlongController's audioSeekRequested
    // carries a `play` flag — a double-click-to-seek wants the narration PLAYING, not toggled
    // (togglePlay would pause an already-playing stream). Idempotent: a no-op if already playing.
    function play() { if (mpv.pause) { mpv.pause = false; session.recordProgress() } }
    function seekRel(delta) {
        var target = Math.max(0, Math.min(mpv.duration, mpv.position + delta))
        session.activityDiscontinuity(target)   // Activity (Lane E): explicit seek
        mpv.seekExact(target)
    }
    function seekTo(t) {
        var target = Math.max(0, t)
        session.activityDiscontinuity(target)   // Activity (Lane E): explicit seek
        mpv.seekExact(target)
    }
    // Read-along: jump to a chapter by its index in chapterModel — a file load for an mp3
    // set, an mpv-chapter seek for a single m4b. No-op if we're already on it.
    function goToChapter(index) {
        if (!session.ready || index < 0 || index >= session.chapterModel.length) return
        var ch = session.chapterModel[index]
        if (ch.kind === "file") {
            if (ch.index !== session.currentIndex) session.playIndex(ch.index)
        } else {
            session.seekTo(Number(ch.time) || 0)
        }
    }
    // Read-along follow: same as goToChapter, but PRESERVE the current play/pause state so
    // turning a page repositions the companion audiobook without force-playing a paused
    // stream (or pausing a playing one). Used by the reader's page-turn sync.
    function playIndexKeepState(i) {
        if (i < 0 || i >= session.files.length) return
        var wasPaused = mpv.pause
        session.pendingResumeSec = -1
        session.currentIndex = i
        session.activityDiscontinuity(0)   // Activity (Lane E): file/chapter source switch
        mpv.loadFile(session.files[i])
        mpv.pause = wasPaused
    }
    function goToChapterKeepState(index) {
        if (!session.ready || index < 0 || index >= session.chapterModel.length) return
        var ch = session.chapterModel[index]
        if (ch.kind === "file") {
            if (ch.index !== session.currentIndex) session.playIndexKeepState(ch.index)
        } else {
            session.seekTo(Number(ch.time) || 0)   // a seek already preserves play state
        }
    }
    function setRate(r) { mpv.speed = r }
    // a real close ends the stream for good (minimize never calls this — remotes just drop away)
    function stop() {
        if (session.ready) session.recordProgress()          // save the spot before the stream dies
        session.activityEndSession()   // Activity (Lane E): real close ends the session
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
                session.playIndex(session.currentIndex + 1)   // intermediate EOF: discontinuity, same session
            else if (reason === "eof")
                session.activityNaturalEof()   // Activity (Lane E): final-file EOF completes once
        }
        function onFileLoaded() {
            if (session.pendingResumeSec > 0) {
                // apply the parked resume seek — and SKIP this heartbeat: mpv.position is
                // still ~0 here (the seek lands async), so recording now would overwrite
                // the saved Continue spot with 0. The 10s Timer records the true spot.
                // Activity: pass the KNOWN resume target explicitly — mpv.position has not
                // caught up to the async seek yet, so reading it here would anchor the
                // tracker's baseline at ~0 and later misread the resume jump as watched time.
                session.activityDiscontinuity(session.pendingResumeSec)
                mpv.seekExact(session.pendingResumeSec)
                session.pendingResumeSec = -1
            } else {
                session.recordProgress()
                session.activityDiscontinuity()   // Activity (Lane E): ordinary (re)load / abnormal-recovery reset
            }
        }
        function onPauseChanged() { session.activityDiscontinuity() }   // Activity: pause/sleep-timer pause
        function onSpeedChanged() { session.activityDiscontinuity() }   // Activity: speed change
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
            session.activityDiscontinuity(pos)   // Activity (Lane E): saved-resume seek
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
    Timer {
        interval: 10000; running: session.ready; repeat: true
        onTriggered: {
            session.recordProgress()
            session.activitySample()   // Activity (Lane E): independent cadence is fine (§9)
        }
    }
    Component.onDestruction: {
        session.recordProgress()
        session.activityEndSession()   // Activity (Lane E): the app itself is closing — end cleanly
    }

    // ── sleep timer (lives here so it survives the page being minimized) ──
    property int sleepMinutes: 0            // 0 = off
    Timer {
        interval: session.sleepMinutes * 60000
        running: session.sleepMinutes > 0
        onTriggered: { mpv.pause = true; session.sleepMinutes = 0 }
    }
}

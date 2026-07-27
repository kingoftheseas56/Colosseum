// PlayerEngineP2 - the Player 2 (D3D11) branch of PlayerEngine.
//
// PlayerEngine forwards ONE fixed surface - mpv's - to whichever branch the PROCESS booted on.
// This file answers that surface with Player2Backend + Player2Session, which share none of mpv's
// names, none of its units and none of its lifecycle signals. Everything below is a translation,
// and every name in it was read out of the headers rather than remembered:
//   native/player2/core/Player2Session.h   (properties, slots, signals)
//   native/player2/core/Player2Types.h     (Player2State, Player2ErrorCode)
//   native/player2/Player2Backend.h/.cpp   (session, attachVideoItem, play's request map)
//
// TASK 3 OWNS CORE TRANSPORT ONLY: open, play/pause, seek, frame-step, speed, volume, mute, video
// fill - and the LIFECYCLE SIGNALS, which the session does not have at all and this file has to
// synthesize (see "the lifecycle" below; without them PlayerPage's loading screen never dismisses
// and recordProgress() never runs at end of file).
// Tracks / subtitle selection / delays are Task 4. mpvProperty()'s stat keys are Task 5. Gating
// PlayerPage's capture and live rows is Task 6. Members those tasks own are still DECLARED here -
// PlayerEngine forwards several of them unguarded, and an undeclared member is a TypeError in the
// middle of a playback, not a build error - but they are left visibly inert, never faked.
import QtQuick
import Colosseum.Player2 1.0

Item {
    id: p2

    // ---- Player2State (native/player2/core/Player2Types.h:15) ------------------------------
    // Named because the whole lifecycle synthesis below is a state machine, and a bare `=== 6`
    // is exactly the kind of line that survives an enum being reordered.
    readonly property int stIdle: 0
    readonly property int stOpening: 1
    readonly property int stBuffering: 2
    readonly property int stPlaying: 3
    readonly property int stPaused: 4
    readonly property int stSeeking: 5
    readonly property int stEnded: 6
    readonly property int stRecovering: 7
    readonly property int stError: 8

    Player2Backend { id: backend }
    Player2VideoItem { id: surface; anchors.fill: parent }

    readonly property var s: backend.session

    // attachVideoItem is REQUIRED, and its absence does not look like an error: the file opens,
    // duration and codec read fine, and nothing ever paints. It is also what installs the
    // player2subtitle image provider (Player2Backend.cpp:98-120). Production twin: the shell's
    // own Component.onCompleted at qml/player2host/Player2Page.qml:255.
    Component.onCompleted: backend.attachVideoItem(surface)

    // ---- state PlayerPage only READS ---------------------------------------------------------
    readonly property real duration: p2.s ? p2.s.duration : 0
    readonly property real position: p2.s ? p2.s.position : 0
    // mpv's cacheTime is "how far the cache reaches ON THE TIMELINE", and it reports 0 when the
    // question does not apply. bufferedSeconds is the same quantity but says -1 for "does not
    // apply" (local file, or an origin that never declared a length), so -1 folds to 0 here.
    readonly property real cacheTime: (p2.s && p2.s.bufferedSeconds >= 0) ? p2.s.bufferedSeconds : 0
    readonly property bool coreSeeking: p2.s ? (p2.s.state === p2.stSeeking) : false
    // MAPPED, not forwarded. The session's row is {index, start, end, title}
    // (Player2Session.cpp:55-60); mpv's is {title, startSec} (native/player/mpvitem.cpp:186-192),
    // and `startSec` is the ONLY field PlayerPage reads. Forwarding the session's shape raw gives
    // undefined on every row, which fails silently and in three places at once:
    //   * chapterAtFraction (PlayerPage.qml:2672-2684) - `(list[i].startSec || 0) <= t` is true for
    //     every row, so the loop never breaks and the seek-hover / chapter HUD always name the LAST
    //     chapter (measured on chaptered.mkv: "Second" at fraction 0.0, where mpv says "First"),
    //   * the seek bar's chapter notches (PlayerPage.qml:4456-4459) - `(startSec || 0) > 1` is
    //     false for all of them, so every notch disappears,
    //   * SkipSegments.chaptersToSegments (qml/SkipSegments.js:33-48) - all starts read as 0.
    // Same principle as the inert track lists below; this one has real data, so it is mapped.
    readonly property var chapters: p2.s ? p2._mapChapters(p2.s.chapters) : []
    function _mapChapters(rows) {
        var out = []
        for (var i = 0; i < (rows || []).length; i++) {
            var c = rows[i] || ({})
            var title = String(c.title || "").trim()
            // mpv substitutes "Chapter" for an untitled chapter (mpvitem.cpp:187-189); matching it
            // keeps the hover tag reading identically on both boots.
            out.push({ "title": title.length ? title : "Chapter",
                       "startSec": Number(c.start || 0) })
        }
        return out
    }
    // The session has no title of its own - it is given one in the play request, and loadFile()
    // (mpv's signature) carries no title. PlayerPage falls back to its own root.mediaTitle.
    property string mediaTitle: ""
    // `url`, matching MpvItem's QUrl: PlayerPage calls mpv.currentUrl.toString() and hands the raw
    // value to SeekThumbnailer.request() (PlayerPage.qml:2808-2809).
    property url currentUrl

    // ---- Task 4 (tracks, subtitle selection, delays) - declared, deliberately inert -----------
    // The session DOES expose audioTracks/subtitleTracks (Player2Session.h:47-48), but in its own
    // shape; PlayerPage's track menus read mpv track objects (id / lang / title / selected). That
    // mapping is Task 4's, so this is an empty list rather than a raw forward of a list whose keys
    // PlayerPage would silently read as undefined.
    readonly property var audioTracks: []
    readonly property var subtitleTracks: []
    property string audioTrack: ""
    property string subtitleTrack: ""
    property real subDelay: 0
    property real audioDelay: 0
    // External subtitles ride PlaybackRequest.externalSubtitles on the OPEN in Player 2 - there is
    // no add-while-playing slot to forward to. Task 4 owns that. Declared because PlayerEngine
    // calls it unguarded (PlayerEngine.qml:199).
    function addSubtitle(url, title, lang, select) {
        console.warn("PlayerEngineP2: addSubtitle is not wired yet (Task 4) - ignored:", url)
    }

    // ---- commands ----------------------------------------------------------------------------

    // Player2Backend::play builds a QUrl from the "url" key and sets stream = !isLocalFile()
    // (Player2Backend.cpp:126-135). A bare Windows path parses as scheme "c", so it would be
    // opened as a STREAM - the file:/// form is not cosmetic. mpv accepted bare paths, and
    // PlayerPage still hands it one (root.mediaLocalPath, PlayerPage.qml:1990).
    function _toUrl(value) {
        var v = String(value || "")
        if (!v.length)
            return ""
        if (/^[a-zA-Z][a-zA-Z0-9+.\-]*:/.test(v) && !/^[a-zA-Z]:[\\\/]/.test(v))
            return v                                   // already carries a scheme (file:, https:, ...)
        return "file:///" + v.replace(/\\/g, "/")
    }

    function loadFile(url) {
        var u = p2._toUrl(url)
        if (!u.length)
            return
        p2.currentUrl = u
        // DISARMED here, armed only by the Opening transition below - not the other way round.
        // Arming at request time is a trap: open() tears the previous playback down first
        // (Player2Session.cpp:453-456), and close()'s resetMediaProperties emits tracksChanged
        // while the OLD file is still in Playing and duration has already been zeroed. That
        // satisfies the rendezvous and fires fileLoaded with duration=0 for a file that has not
        // been opened yet (measured on a back-to-back A->B open, 2026-07-27). Opening is the only
        // honest "this playback has begun" marker, and every successful play() reaches it.
        p2._awaitingLoad = false
        p2._endedFired = false
        p2._metadataReady = false
        // play() RETURNS a decision map; ignoring it makes a decline look like a hang.
        var decision = backend.play({
            "url": u,
            "mediaId": "",
            "title": "",
            "resumeSeconds": 0,          // PlayerPage owns resume: it seeks from its own onFileLoaded
            "live": false,
            "headers": ({})              // the debrid seam - Task 8 carries real headers through here
        })
        if (String((decision && decision.outcome) || "") === "player2")
            return
        // Player 2 declined outright. Nothing has been shown, and there is no second backend in
        // this PROCESS (the RHI is a boot choice), so PlayerPage's own error surface is the only
        // honest destination. Nothing to disarm: a decline never reaches Opening.
        p2.playbackError("declined", String((decision && decision.reason) || "Player 2 declined this playback"))
    }

    function seekExact(sec) { if (p2.s) p2.s.seekExact(sec) }
    // mpv's seekStep is RELATIVE. seekRelative() is the session's own equivalent and it clamps at
    // zero inside seekExact (Player2Session.cpp:551), so no arithmetic belongs here.
    function seekStep(delta) { if (p2.s) p2.s.seekRelative(delta) }
    function frameStep() { if (p2.s) p2.s.frameStep(1) }
    function frameBackStep() { if (p2.s) p2.s.frameStep(-1) }

    // mpv's three loudness names against NormalizationMode (Player2Types.h:29). mpv's "off" IS
    // Player 2's Smooth: the no-extra-filter default, not a fourth mode.
    function setAudioNormalization(mode) {
        if (!p2.s)
            return
        p2.s.setNormalizationMode(mode === "full" ? 2 : (mode === "light" ? 1 : 0))
    }

    // ---- state PlayerPage ASSIGNS ------------------------------------------------------------
    // Every push below is guarded by _applying, which is raised only while this file is writing
    // the ENGINE's value back into itself. Without it each adopt-back would be re-pushed into the
    // session as a fresh command.
    property bool _applying: false

    // play() from Idle, Ended or Error is an ILLEGAL transition, and a rejected transition emits
    // errorOccurred(InvalidCommand) (Player2Session.cpp:686-692) - which reaches PlayerPage as a
    // playbackError and starts its recovery ladder over a button press. So the push is gated on
    // the transport actually being able to take the command.
    // KNOWN LIMITATION, not a workaround: pressing play once playback has ENDED does nothing here.
    // Ended only accepts Opening / Seeking / Idle, so replay-from-EOF needs a real re-open; mpv
    // restarted the file instead. Left visible for the arc to answer, not papered over.
    //
    // RELATED, and worse (measured 2026-07-27, engine-side, NOT caused by this file): rewinding a
    // clip whose demux has already reached EOF strands playback - the session says Playing and
    // position never moves. It then reaches Ended a SECOND time, so endFile("eof") fires twice and
    // PlayerPage starts Up Next twice (endFiles=[eof|eof]). Reproduces on the 2s av.mkv fixture;
    // does NOT reproduce on real-length media. Task 7 owns verifying the progress/Up Next path.
    function _transportLive() {
        if (!p2.s)
            return false
        var st = p2.s.state
        return st === p2.stOpening || st === p2.stBuffering || st === p2.stPlaying
               || st === p2.stPaused || st === p2.stSeeking || st === p2.stRecovering
    }

    property bool pause: false
    onPauseChanged: {
        if (p2._applying || !p2._transportLive())
            return
        if (p2.pause)
            p2.s.pause()
        else
            p2.s.play()
    }

    // THE CLAMP TRAP PlayerEngine.qml:55-69 warns about, and Player 2 is where it is real:
    // PlayerPage clamps speed to 0.25..3 (PlayerPage.qml:2590-2591, and 3025 pushes >=2), while
    // Player2Session clamps to 0.5..2.0 and RETURNS SILENTLY when the clamped value equals the one
    // it already holds (Player2Session.cpp:656-665). Push 3 while the session is already at 2.0
    // and nothing is emitted - the facade would keep 3 for the rest of the session. Re-reading the
    // engine's real value after every push is what closes that.
    property real speed: 1
    onSpeedChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setSpeed(p2.speed)
        p2._adoptSpeed()
    }
    function _adoptSpeed() {
        if (!p2.s || p2.speed === p2.s.speed)
            return
        p2._applying = true
        p2.speed = p2.s.speed
        p2._applying = false
    }

    // mpv's surface is 0..100 (an int). The session's volume is a linear float 0..1.
    property int volume: 100
    onVolumeChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setVolume(p2.volume / 100)
        p2._adoptVolume()
    }
    function _adoptVolume() {
        if (!p2.s)
            return
        var v = Math.round(p2.s.volume * 100)
        if (p2.volume === v)
            return
        p2._applying = true
        p2.volume = v
        p2._applying = false
    }

    // The session's property is `muted`; mpv's surface member is `mute`. Not the same name.
    property bool mute: false
    onMuteChanged: { if (!p2._applying && p2.s) p2.s.setMuted(p2.mute) }

    // panscan, videoZoom and videoAspect all report through ONE signal on the session, exactly as
    // they do on MpvItem - hence the explicit signal below rather than the three generated ones,
    // which is what PlayerEngine's relay connects to (PlayerEngine.qml:167).
    property real panscan: 0
    property real videoZoom: 0
    property string videoAspect: ""
    signal videoFillChanged()
    onPanscanChanged: { if (!p2._applying && p2.s) p2.s.setPanscan(p2.panscan) }
    onVideoZoomChanged: { if (!p2._applying && p2.s) p2.s.setVideoZoom(p2.videoZoom) }
    onVideoAspectChanged: { if (!p2._applying && p2.s) p2.s.setVideoAspect(p2.videoAspect) }

    // ---- the lifecycle -----------------------------------------------------------------------
    // Player2Session emits NONE of these. They are synthesized from its state machine below, and
    // PlayerPage's whole opening sequence hangs off them: fileStarted raises the loading screen,
    // fileLoaded dismisses it and applies the resume seek, endFile("eof") is what calls
    // recordProgress() and starts Up Next.
    signal fileStarted()
    signal fileLoaded()
    signal playbackError(string code, string message)
    signal endFile(string reason)
    signal trackListChanged()
    // Player 2 has no capture pipeline. Declared so PlayerEngine's relay is not a dead connection
    // (its Connections deliberately omits ignoreUnknownSignals, so a missing signal is a loud
    // runtime warning). NEVER emitted - PlayerEngine.supportsCapture is false and Task 6 gates the
    // controls that would have used them.
    signal gifSaved(string path)
    signal gifFailed()

    property int _prevState: -1
    property bool _awaitingLoad: false
    property bool _endedFired: false
    // Metadata has landed for THIS open. The session publishes duration, tracks and chapters and
    // THEN transitions to Playing, all in one slot (Player2Session.cpp:40-64), so tracksChanged is
    // a reliable "the media is described" marker that arrives strictly before the open's Playing.
    // It is what stops a Playing that is NOT the open from consuming the one-shot fileLoaded gate:
    // Opening -> Playing is a legal transition, so a pause=false push during Opening would fire
    // fileLoaded with duration=0 and then never re-arm for the real open (measured). Not reachable
    // through PlayerPage today - togglePlayPause is gated on !root.starting (PlayerPage.qml:2478)
    // and every other pause writer needs root.fileReady - but the gate costs one boolean.
    // Chosen over a duration>0 test on purpose: a live or lengthless stream has no duration and
    // would never load at all under that check.
    property bool _metadataReady: false

    // fileLoaded is a RENDEZVOUS of two facts - "the media is described" and "the transport is
    // running" - and either can land first, so it is evaluated from both sides instead of from the
    // state change alone. Ordering the normal way round (metadata, then Playing) is not something
    // this file may assume: if something drives Opening -> Playing early, the session's own
    // transition(Playing) that follows the metadata is a no-op and emits NOTHING (transition()
    // only signals when result.changed, Player2Session.cpp:686-703), so a state-only check would
    // never fire fileLoaded at all - the loading screen would sit there forever over a playing
    // file. Measured both ways: without the metadata half it fires early with duration=0 and never
    // re-arms; with the metadata half but no rendezvous it never fires (2026-07-27).
    function _maybeFileLoaded() {
        if (!p2._awaitingLoad || !p2._metadataReady || !p2.s)
            return
        var st = p2.s.state
        if (st !== p2.stPlaying && st !== p2.stPaused)
            return
        p2._awaitingLoad = false
        p2.fileLoaded()
    }

    // Player2ErrorCode (Player2Types.h:37) -> the strings PlayerPage's handlePlaybackIssue
    // branches on (PlayerPage.qml:1253-1262): "network" and "decode"/"codec" drive its retry
    // ladder, everything else falls through to the message.
    function _errorCode(code) {
        switch (Number(code)) {
        case 1: return "cancelled"          // Cancelled
        case 2: return "open"               // OpenFailed
        case 3: return "hardware"           // UnsupportedHardware
        case 4: return "decode"             // DecodeFailed
        case 5: return "network"            // NetworkFailed
        case 6: return "device"             // DeviceLost
        case 7: return "device"             // AudioDeviceLost
        case 8: return "invalid-command"    // InvalidCommand
        }
        return "unknown"
    }

    Connections {
        target: p2.s

        function onStateChanged() {
            var st = p2.s.state
            var prev = p2._prevState
            p2._prevState = st
            if (st === prev)
                return

            if (st === p2.stOpening) {
                p2._awaitingLoad = true
                p2._endedFired = false
                // Cleared HERE and not only in loadFile(): open() calls resetMediaProperties()
                // first, which itself emits tracksChanged (Player2Session.cpp:734-737) - that one
                // must not pre-arm the gate for the open that follows it.
                p2._metadataReady = false
                p2.fileStarted()
            } else {
                // "loaded" = the pipeline is RUNNING, with duration and tracks settled.
                // NOT keyed on `generation`, despite what the plan assumed: seekExact() advances
                // the generation too (Player2Session.cpp:549), so a per-generation gate would
                // re-fire fileLoaded on every seek - and PlayerPage's onFileLoaded re-applies the
                // pending resume seek and re-runs track automation. Keyed on "the last thing this
                // engine did was OPEN" instead, which is what mpv's fileLoaded actually means.
                p2._maybeFileLoaded()
            }

            if (st === p2.stEnded) {
                // ENTERING Ended is end-of-file. demuxEnded(DemuxEndReason) carries the same fact,
                // but DemuxEndReason has no Q_ENUM_NS (DemuxSession.h:27) - it IS a registered
                // metatype (Q_DECLARE_METATYPE at DemuxSession.h:219, qRegisterMetaType at
                // DemuxSession.cpp:116), so it crosses to C++ fine; what it lacks is the enum
                // registration that would make its VALUE meaningful in QML. The state transition
                // it causes is typed, observable, and already deduplicated by the state machine,
                // so that is what this rides.
                //
                // ⚠ FOR TASK 7: PlayerPage runs BOTH recordProgress() and startUpNextCountdown()
                // on every "eof" (PlayerPage.qml:2876-2881). See the eof-rewind note on
                // _transportLive below - a stranded rewind reaches Ended twice, so Up Next fires
                // TWICE. Verifying that is Task 7's, not fixed here.
                if (!p2._endedFired) {
                    p2._endedFired = true
                    p2.endFile("eof")       // PlayerPage branches on exactly this string
                }
            } else if (st === p2.stError) {
                if (!p2._endedFired) {
                    p2._endedFired = true
                    p2._awaitingLoad = false
                    p2.endFile("error")     // not "eof": must NOT record progress or start Up Next
                }
            } else {
                // Left the terminal states - a seek back out of EOF must be able to end again.
                p2._endedFired = false
            }

            // The transport button mirrors only the two states that ARE a pause decision. Opening,
            // Buffering and Seeking are transient; mirroring them would make the button flicker
            // through every buffer hiccup.
            if (st === p2.stPlaying || st === p2.stPaused) {
                var wantPause = (st === p2.stPaused)
                if (p2.pause !== wantPause) {
                    p2._applying = true
                    p2.pause = wantPause
                    p2._applying = false
                }
            }
        }

        function onErrorOccurred(error) {
            p2.playbackError(p2._errorCode(error ? error.code : 0),
                             String((error && error.message) || ""))
        }

        function onTracksChanged() {
            p2._metadataReady = true
            p2.trackListChanged()
            p2._maybeFileLoaded()   // the other half of the rendezvous
        }
        function onSpeedChanged() { p2._adoptSpeed() }
        function onVolumeChanged() { p2._adoptVolume() }
        function onMutedChanged() {
            if (p2.mute === p2.s.muted)
                return
            p2._applying = true
            p2.mute = p2.s.muted
            p2._applying = false
        }
        function onVideoFillChanged() {
            p2._applying = true
            if (p2.panscan !== p2.s.panscan) p2.panscan = p2.s.panscan
            if (p2.videoZoom !== p2.s.videoZoom) p2.videoZoom = p2.s.videoZoom
            if (p2.videoAspect !== p2.s.videoAspect) p2.videoAspect = p2.s.videoAspect
            p2._applying = false
            p2.videoFillChanged()
        }
    }
}

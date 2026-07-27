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
// TASK 3 OWNED CORE TRANSPORT: open, play/pause, seek, frame-step, speed, volume, mute, video fill
// - and the LIFECYCLE SIGNALS, which the session does not have at all and this file has to
// synthesize (see "the lifecycle" below; without them PlayerPage's loading screen never dismisses
// and recordProgress() never runs at end of file).
// TASK 4 ADDS: the track lists (mapped into the shape PlayerPage's row builders read), track
// SELECTION, the two delays, the external-subtitle disposition, subtitle STYLING, and the mount of
// SubtitleLayer - the one piece of Player 2's own chrome that survives the port, because on this
// boot the subtitle is not burned into the frame, it is a QML item somebody has to draw.
// mpvProperty()'s stat keys are Task 5. Gating PlayerPage's capture / live / external-subtitle rows
// is Task 6, on the capability flags PlayerEngine publishes. Members those tasks own are still
// DECLARED here - PlayerEngine forwards several of them unguarded, and an undeclared member is a
// TypeError in the middle of a playback, not a build error - but they are left visibly inert,
// never faked.
import QtQuick
import Colosseum.Player2 1.0
// SubtitleLayer lives beside the rest of Player 2's controls until Task 9 moves it next to this
// file; the shell it was written for (qml/player2/Player2Shell.qml) mounts it the same way.
import "player2/controls"

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

    // mpv burns subtitles into the frame; Player 2 hands cues to QML. This layer IS the subtitle
    // renderer on a P2 boot, mounted over the video item so cues sit on the letterboxed picture -
    // and inside the engine so it stays below PlayerPage's chrome and survives the chrome hiding,
    // exactly as a burned-in subtitle does on the mpv boot.
    // It also carries the six subtitle STYLE controls: with no sub-* option surface in Player 2,
    // this Text is where scale / colour / outline / position actually happen (see setSubOption).
    SubtitleLayer {
        anchors.fill: surface
        session: p2.s
        subScale: p2.subScale
        subColor: p2.subColor
        subBorderSize: p2.subBorderSize
        subBorderColor: p2.subBorderColor
        subPos: p2.subPos
    }

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

    // ---- tracks --------------------------------------------------------------------------------
    // MAPPED, not forwarded, for exactly the reason the chapters above are. The session's row is
    // {index, type, codec, language, title, default, forced} (Player2Session.cpp:44-52) and the
    // lists are already filtered by type (Player2Session.cpp:262,271). PlayerPage's audioRow() and
    // subtitleRow() (PlayerPage.qml:372-427) read `id`, `lang`, `title`, `label`, `codec`,
    // `channels`, `external`, `forced`, `default`, `selected`, `url` off the RAW row, and its
    // menus, its language automation (TrackLanguage.js) and its audio/subs chips all read what
    // those two builders return. `index` vs `id` and `language` vs `lang` alone would leave every
    // menu row labelled "Audio track" with no id to select by - the chapters bug one property over.
    //
    // `selected` has NO session equivalent: the session never publishes which track is on, it
    // REPORTS a switch through audioTrackChanged/subtitleTrackChanged. So it is derived from the id
    // the engine last reported applying - which is why both lists depend on audioTrack /
    // subtitleTrack and re-evaluate the moment the engine adopts a new one.
    //
    // The empty defaults are honest, not placeholders. FFmpeg's DemuxStreamInfo
    // (DemuxSession.h:34-43) carries no channel count and no bitrate, and Player 2 has no external
    // subtitle path at all, so `channels`, `url` and `external` have nothing to give. PlayerPage
    // degrades correctly on them: trackTech() simply prints the codec, and trackBitrate() reads
    // `demux-bitrate` which was never there on this branch.
    readonly property var audioTracks: p2._mapTracks(p2.s ? p2.s.audioTracks : [], p2.audioTrack, true)
    readonly property var subtitleTracks: p2._mapTracks(p2.s ? p2.s.subtitleTracks : [], p2.subtitleTrack, false)

    function _mapTracks(rows, activeId, isAudio) {
        var out = []
        for (var i = 0; i < (rows || []).length; i++) {
            var t = rows[i] || ({})
            // A STRING id, like MpvItem::stringifyId's (mpvitem.cpp:729-733). PlayerPage does
            // `String(track.id || "")`, and a NUMERIC 0 is falsy there - it would erase stream 0's
            // id entirely. Strings make that impossible for every index.
            var id = (t.index === undefined || t.index === null) ? "" : String(t.index)
            var lang = String(t.language || "").trim()
            // mpv's own fallback ladder (mpvitem.cpp:707-715): title, then language, then a generic
            // word. PlayerPage falls back again on top of this, so matching mpv here is what makes
            // the two boots' menus read identically instead of merely both being non-empty.
            var title = String(t.title || "").trim()
            if (!title.length) title = lang
            if (!title.length) title = isAudio ? "Audio track" : "Subtitle"
            out.push({
                "id": id,
                "type": isAudio ? "audio" : "sub",
                "title": title,
                "lang": lang,
                "codec": String(t.codec || ""),
                "channels": "",
                "external": false,
                // mpv also calls a track forced when its TITLE says so (mpvitem.cpp:720-723).
                "forced": !!t.forced || title.toLowerCase().indexOf("forced") >= 0,
                // Bracket access: `default` is a reserved word, and this row is built from a map
                // whose key genuinely is "default" (Player2Session.cpp:50).
                "default": !!t["default"],
                "selected": id.length > 0 && id === String(activeId),
                "url": ""
            })
        }
        return out
    }

    // ---- track selection -----------------------------------------------------------------------
    // The session has NO audioTrack/subtitleTrack property to read back. selectAudioTrack() and
    // selectSubtitleTrack() are fire-and-forget slots that post a command to the demux thread, and
    // the demux REPORTS what it actually applied through audioTrackChanged(gen, streamIndex) /
    // subtitleTrackChanged(gen, streamIndex) (DemuxSession.cpp:1054, 1070, 1235). Those reports
    // carry the index that is really decoding: a select for a stream that is not audio leaves the
    // old index, and a subtitle decoder that fails to open comes back as -1. They, not the push,
    // are what these two properties hold - so the tick in the menu is the engine's answer, never
    // an echo of the click.
    property string audioTrack: ""
    property string subtitleTrack: ""
    // The last value the ENGINE reported. A push the session REFUSES emits nothing at all, and the
    // facade's relay re-syncs only on a change signal (PlayerEngine.qml:57-69), so a refused push
    // has to be walked back here or PlayerPage shows a track that is not playing, permanently.
    property string _appliedAudioTrack: ""
    property string _appliedSubtitleTrack: ""

    // Player2Session::hasActiveMedia (Player2Session.cpp:527-539) - every selection slot returns
    // early and silently when this is false. Mirrored rather than inferred from a timeout: it is a
    // pure function of the state this file already watches, so the answer is exact and synchronous.
    // NOT the same set as _transportLive(): Opening and Recovering have no media to select on.
    function _mediaActive() {
        if (!p2.s)
            return false
        var st = p2.s.state
        return st === p2.stPlaying || st === p2.stPaused || st === p2.stBuffering
               || st === p2.stSeeking || st === p2.stEnded
    }

    // Write the ENGINE's value into one of our own properties without pushing it back down.
    function _adoptString(key, value) {
        if (p2[key] === value)
            return
        p2._applying = true
        p2[key] = value
        p2._applying = false
    }

    onAudioTrackChanged: {
        if (p2._applying)
            return
        // selectAudioTrack parses the id as an int and returns silently when it does not parse
        // (Player2Session.cpp:576-587). mpv's "" - which MpvItem turns into `aid=no`, i.e. AUDIO
        // OFF - is therefore not understood by Player 2, it is simply dropped. There is no
        // audio-off command on this engine, so the honest answer is to walk the facade back to the
        // track that is really decoding rather than let it claim audio is off.
        if (!p2._mediaActive() || !/^-?\d+$/.test(String(p2.audioTrack).trim())) {
            p2._adoptString("audioTrack", p2._appliedAudioTrack)
            return
        }
        p2.s.selectAudioTrack(String(p2.audioTrack))
    }

    onSubtitleTrackChanged: {
        if (p2._applying)
            return
        // selectSubtitleTrack DOES understand mpv's off sentinel: empty / "-1" / "off" all disable
        // (Player2Session.cpp:589-602), and "" is exactly what PlayerPage assigns for Off
        // (turnSubtitlesOff, PlayerPage.qml:670-674), so the string passes straight through with no
        // mapping invented on top. Only the hasActiveMedia guard can refuse it.
        if (!p2._mediaActive()) {
            p2._adoptString("subtitleTrack", p2._appliedSubtitleTrack)
            return
        }
        p2.s.selectSubtitleTrack(String(p2.subtitleTrack))
    }

    // ---- delays ----------------------------------------------------------------------------------
    // Player2Session::setSubDelay / setAudioDelay (Player2Session.cpp:605-620) store the seconds
    // EXACTLY - no clamp and no rounding, unlike MpvItem which rounds both to 2dp
    // (mpvitem.cpp:637,647) - so the only way they can absorb a push is the qFuzzyCompare no-op
    // when the value is already held. Re-read anyway, for the same reason speed and volume do: the
    // relay's one re-sync is a change signal, so a silently-absorbed push would strand the facade
    // on a number the engine never took.
    //
    // KNOWN BEHAVIOURAL GAP vs mpv, engine-side and not papered over here: the sub delay is applied
    // to each cue AS IT ARRIVES (Player2Session.cpp:184-188), so changing it re-times only cues that
    // have not been decoded yet - cues already buffered keep their old timing for a few seconds.
    // mpv re-times the whole subtitle stream at once. The control is real and it does reach the
    // engine; it just settles over the read-ahead rather than instantly.
    property real subDelay: 0
    property real audioDelay: 0
    onSubDelayChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setSubDelay(p2.subDelay)
        p2._adoptSubDelay()
    }
    onAudioDelayChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setAudioDelay(p2.audioDelay)
        p2._adoptAudioDelay()
    }
    function _adoptSubDelay() {
        if (!p2.s || p2.subDelay === p2.s.subDelay)
            return
        p2._applying = true
        p2.subDelay = p2.s.subDelay
        p2._applying = false
    }
    function _adoptAudioDelay() {
        if (!p2.s || p2.audioDelay === p2.s.audioDelay)
            return
        p2._applying = true
        p2.audioDelay = p2.s.audioDelay
        p2._applying = false
    }

    // ---- external subtitles: NOT SUPPORTED, and said so out loud --------------------------------
    // PlaybackRequest carries an externalSubtitles field (Player2Types.h:87/98) and NOTHING reads
    // it - not Player2Backend::play, not Player2Session::open, not DemuxSession (grepped: the only
    // non-generated hits in the tree are the declaration itself and a metatype test). So Player 2
    // has no external-subtitle path AT ALL, open-time or otherwise, and there is nothing for this
    // function to forward to.
    // PlayerEngine.supportsExternalSubs is false on this branch; Task 6 gates every PlayerPage row
    // that would reach here (the OpenSubtitles list, subtitle file drag-and-drop, and pickSubtitle's
    // "ext:" route, PlayerPage.qml:299-336). This stays LOUD rather than silent on purpose: if it is
    // ever called, a gate is missing and that must be visible, not swallowed.
    function addSubtitle(url, title, lang, select) {
        console.warn("PlayerEngineP2: Player 2 has no external-subtitle seam "
                     + "(PlayerEngine.supportsExternalSubs=false) - ignored:", url)
    }

    // ---- subtitle styling: the SubtitleLayer above IS the implementation ------------------------
    // SubStyleBar takes the ENGINE as `player:` (PlayerPage.qml:2935) and calls setSubOption() for
    // six keys (SubStyleBar.qml:52-58). On the mpv boot those are real mpv options and mpv redraws
    // the burned-in subtitle. Player 2 has no option surface of any kind - but it does not need one:
    // on this boot the cue is a QML Text that SubtitleLayer draws, so scale, colour, outline width,
    // outline colour and position are all genuinely implementable, and implementing them is what
    // keeps the six controls working instead of moving and doing nothing.
    //
    // sub-ass-override is the ONE key with nothing to honour, and it is gated rather than faked:
    // the ASS override blocks are stripped in C++ before a cue ever reaches QML (plainFromAss,
    // SubtitlePipeline.cpp:26-42 removes every {\...} run), so there is no embedded styling left to
    // keep, scale or force. PlayerEngine.supportsSubAssOverride is false on this branch and
    // SubStyleBar hides that one cluster.
    property real subScale: 1.0
    property color subColor: "#ffffff"
    property real subBorderSize: 2.0
    property color subBorderColor: "#000000"
    property int subPos: 92
    function setSubOption(key, value) {
        switch (String(key)) {
        case "sub-scale":        p2.subScale = Number(value); return
        case "sub-color":        p2.subColor = String(value); return
        case "sub-border-size":  p2.subBorderSize = Number(value); return
        case "sub-border-color": p2.subBorderColor = String(value); return
        case "sub-pos":          p2.subPos = Math.round(Number(value)); return
        case "sub-ass-override": return   // nothing to honour - see the block comment above
        }
        console.warn("PlayerEngineP2: unknown subtitle option ignored:", key, value)
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
                // The previous file's stream indices mean nothing in the new one. Cleared here so a
                // stale id cannot tick a row of the NEW track list (they are plain integers, so
                // "1" from the last file happily matches a different "1" in this one) - and cleared
                // through _adoptString so PlayerPage is told, rather than left holding it.
                p2._appliedAudioTrack = ""
                p2._appliedSubtitleTrack = ""
                p2._adoptString("audioTrack", "")
                p2._adoptString("subtitleTrack", "")
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

        // The demux's own report of the stream that is now decoding - not an echo of our push. It
        // also fires UNPROMPTED once per open, with the track the policy picked (DemuxSession.cpp:
        // 1235), which is how audioTrack gets its starting value and how the first tick appears in
        // PlayerPage's audio menu without anything having been clicked.
        // The generation argument is deliberately ignored: Player2Session has already filtered
        // these on m_generation.accepts() before re-emitting (Player2Session.cpp:159-179).
        function onAudioTrackChanged(generation, streamIndex) {
            p2._appliedAudioTrack = streamIndex >= 0 ? String(streamIndex) : ""
            p2._adoptString("audioTrack", p2._appliedAudioTrack)
        }
        // -1 here is a real answer, not an absence: it is what the demux reports when subtitles were
        // turned OFF and when the chosen subtitle decoder failed to open (DemuxSession.cpp:1060-1070).
        // Either way "" is what PlayerPage reads as off, which is what is true.
        function onSubtitleTrackChanged(generation, streamIndex) {
            p2._appliedSubtitleTrack = streamIndex >= 0 ? String(streamIndex) : ""
            p2._adoptString("subtitleTrack", p2._appliedSubtitleTrack)
        }
        function onSubDelayChanged() { p2._adoptSubDelay() }
        function onAudioDelayChanged() { p2._adoptAudioDelay() }
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

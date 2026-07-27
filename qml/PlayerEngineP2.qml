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
    // A third distinct set, alongside _transportAcceptsPlay/_transportAcceptsPause below: Opening
    // and Recovering have no media to select on, and Ended still does.
    function _mediaActive() {
        if (!p2.s)
            return false
        var st = p2.s.state
        return st === p2.stPlaying || st === p2.stPaused || st === p2.stBuffering
               || st === p2.stSeeking || st === p2.stEnded
    }

    // Write the ENGINE's value into one of our own properties without pushing it back down.
    // THE one adopt-back in this file - every re-sync goes through here. It exists for the same
    // reason PlayerEngine collapsed its relay to _push/_pull: a hand-written `_applying = true;
    // x = v; _applying = false` sandwich per member is a transposition waiting to happen, and the
    // one that held _applying across three paired assignments at once (the video-fill adopt) was
    // the widest such window in the file.
    function _adopt(key, value) {
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
            p2._adopt("audioTrack", p2._appliedAudioTrack)
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
            p2._adopt("subtitleTrack", p2._appliedSubtitleTrack)
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
        p2._adopt("subDelay", p2.s.subDelay)
    }
    onAudioDelayChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setAudioDelay(p2.audioDelay)
        p2._adopt("audioDelay", p2.s.audioDelay)
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
    // PlayerPage still hands it one (root.mediaLocalPath, PlayerPage.qml:1991).
    //
    // This is a STRING transform, which is the wrong tool for the job and is here only because QML
    // has no QUrl::fromLocalFile. The proper fix is to let C++ build the QUrl - Player2Backend::play
    // is one hop away and already holds the string - and this should move there when the request map
    // next changes (Task 8 carries headers through the same call). Until then it handles the three
    // shapes PlayerPage actually produces - drive-letter path, UNC path, already-schemed URL - and
    // percent-escapes the two characters QUrl would otherwise read as syntax:
    //   * `#` in a filename becomes a FRAGMENT and silently truncates the path,
    //   * `?` becomes a QUERY and does the same.
    // Both are legal in Windows filenames. They are escaped only on the local-path branch, never on
    // an already-schemed URL, where a real query string must survive intact.
    function _toUrl(value) {
        var v = String(value || "")
        if (!v.length)
            return ""
        if (/^[a-zA-Z][a-zA-Z0-9+.\-]*:/.test(v) && !/^[a-zA-Z]:[\\\/]/.test(v))
            return v                                   // already carries a scheme (file:, https:, ...)
        var p = v.replace(/\\/g, "/").replace(/#/g, "%23").replace(/\?/g, "%3F")
        // UNC: \\server\share\file -> file://server/share/file. The host belongs BEFORE the path,
        // so the usual three slashes would turn the server name into a directory.
        if (p.indexOf("//") === 0)
            return "file:" + p
        return "file:///" + p
    }

    // mpv's raw command channel. Only TWO verbs reach here through PlayerEngine - grepped every
    // `.command(` call site in qml/ - and they are answered separately because only one of them
    // means anything on this engine:
    //
    //   ["stop"]  - PlayerPage.stop() (PlayerPage.qml:2129), reached by Main.qml's closePlayer
    //               (Main.qml:955) and its close-a-session path (Main.qml:1179, whose own comment
    //               is "a real close ends the stream for good"). This is NOT optional: without it a
    //               closed player leaves a LIVE session behind - audio still playing, demux, decode
    //               and the pump still burning GPU on a window that is gone. Player2Backend::stop
    //               (Q_INVOKABLE, Player2Backend.h:47) stops the pump and closes the session, which
    //               is exactly mpv's stop.
    //
    //   ["set", "aid", ...] - healAudio() (PlayerPage.qml:1639-1646), which drops and re-picks the
    //               SAME audio track to force a fresh device connection. It exists for a documented
    //               mpv defect ("audio-stream-silence in mpvitem.cpp"): Windows kills mpv's idle
    //               audio stream while the app is parked. Player 2 does not share that stack - it
    //               has its own WASAPI sink and a DeviceRecoveryCoordinator that handles
    //               AudioDeviceLost - so replaying mpv's workaround here would be inventing a cure
    //               for a disease this engine has not been shown to have. Recognised and refused on
    //               purpose, not unhandled. ⚠ OPEN, for the arc: whether P2's own recovery covers
    //               the park-and-resume case is UNVERIFIED, and it needs a real un-minimize soak.
    //
    // Anything else is a verb nobody has considered on this branch, and it says so out loud rather
    // than evaporating - which is precisely how the stop above went missing until review.
    function command(args) {
        var a = args || []
        var verb = String(a[0] || "")
        if (verb === "stop") {
            if (p2.s)
                backend.stop()
            // mpv clears its path on stop, so re-opening the SAME file afterwards still counts as a
            // new url. Without this, currentUrl would not change and PlayerPage would keep the
            // previous playback's seek thumbnails (its onCurrentUrlChanged is the reset).
            p2.currentUrl = ""
            return
        }
        if (verb === "set" && String(a[1] || "") === "aid")
            return                                     // see healAudio above - refused deliberately
        console.warn("PlayerEngineP2: unhandled engine command ignored:", JSON.stringify(a))
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
        p2._metadataReady = false
        // (_endedFired is not touched here or at Opening: the state handler's terminal-state chain
        // is its single owner, and every non-terminal state reaches it.)
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

    // ---- mpvProperty: the twelve keys PlayerPage asks the engine by NAME (Task 5) --------------
    //
    // On the mpv boot libmpv answers these itself. Player 2 has no property bag by design
    // (PlaybackDiagnostics.h:9-12 says so out loud), so this is the whole translation, and it is
    // held to one rule: every value is either MEASURED or "". A plausible number here would read
    // as an instrument, and the stats card, the pause card and the recovery watchdog all believe
    // it. PlayerPage renders "" as "--" (statsValue, PlayerPage.qml:2439) - honest absence.
    //
    // TWO of the twelve are not stats rows at all, which is why they are sourced first and hardest:
    //   width/height        - tickRecoveryWatch (PlayerPage.qml:1277-1288) latches recoverySawVideo
    //                         off these; a permanent 0 makes a HEALTHY stream fail "no video" after
    //                         noVideoGraceSeconds. They also print the pause card's "1080p"
    //                         (pauseQualityLine, PlayerPage.qml:2757). NOT derivable from
    //                         `inputFormat`: that field is the DXGI PIXEL format - "NV12", "P010",
    //                         "DXGI_<n>" (D3D11VideoPipeline.cpp formatName) - and never a
    //                         resolution, so the size had to be published from the decoded frame
    //                         itself (diagnostics sourceWidth/sourceHeight).
    //   cache-buffering-state - the live "Buffering N%" state line (PlayerPage.qml:220-225).
    //                         Answered "" deliberately; see the case below.
    function mpvProperty(name) {
        var d = (p2.s && p2.s.diagnostics) ? p2.s.diagnostics() : ({})
        switch (String(name)) {

        // --- measured ---------------------------------------------------------------------
        case "video-codec":
            return String(d.videoCodec || "")
        // mpv names the active hwdec ("d3d11va"); PlaybackDiagnostics.hardwareFormat carries
        // exactly that string for the decode path (D3D11VideoPipeline.cpp submitDecodedFrame).
        case "hwdec-current":
            return String(d.hardwareFormat || "")
        // The two drop counters PlayerPage prints as "decoder / output"
        // (statsValue "Dropped frames", PlayerPage.qml:2460). `dropped` is producer starvation -
        // decoded frames that never reached the ring; `scheduledLateDrops` is the frame scheduler
        // discarding a frame that arrived too late to present, which is the output-side drop.
        case "frame-drop-count":
            return Number(d.dropped || 0)
        case "vo-drop-frame-count":
            return Number(d.scheduledLateDrops || 0)
        // 0 until a frame has actually been presented, and back to 0 across a flush - report that
        // as ABSENT, not as "0x0" (statsValue reads 0 as absent too, but the watchdog reads the
        // raw value and mpvClean() would print a literal "0" into the pause card).
        case "width":
            return Number(d.sourceWidth || 0) > 0 ? Number(d.sourceWidth) : ""
        case "height":
            return Number(d.sourceHeight || 0) > 0 ? Number(d.sourceHeight) : ""
        // mpv reports the codec of the track that is PLAYING. diagnostics.videoCodec is already
        // resolved that way for video; audio has a selectable track, so resolve it against the
        // index the DEMUX reported as decoding (p2.audioTrack), never against "the first audio
        // stream" - on a two-language file that names the wrong codec the moment he switches.
        // Empty until the demux has reported, which is a fact and not a gap to paper over.
        case "audio-codec": {
            var rows = (p2.s && p2.s.audioTracks) ? p2.s.audioTracks : []
            for (var i = 0; i < rows.length; i++) {
                var t = rows[i] || ({})
                if (String(t.index) === String(p2.audioTrack))
                    return String(t.codec || "")
            }
            return ""
        }

        // --- honest absences: the engine has no seam for these yet (Task 10 follow-ups) --------
        // Each one is a REAL missing measurement, not a mapping this file declined to write.
        //   container-fps      - the container's declared frame rate. DemuxStreamInfo
        //                        (DemuxSession.h:34-43) carries index/type/codec/language/title/
        //                        default/forced and nothing else; avg_frame_rate is read by nobody.
        //   estimated-vf-fps   - the OBSERVED output rate. The pipeline counts `presented` but
        //                        stamps no wall-clock interval, so there is no rate to divide.
        //   video/audio-bitrate- observed bitrates. DemuxSession knows every packet's size and pts
        //                        (it already sums consumedBytes for the buffer estimate) but keeps
        //                        no per-stream windowed total.
        // Deriving any of them here would mean inventing the denominator, and the invented number
        // would look exactly like a measured one. "" prints "--".
        case "container-fps":
        case "estimated-vf-fps":
        case "video-bitrate":
        case "audio-bitrate":
            return ""
        // cache-buffering-state is mpv's INITIAL-FILL percentage: bytes held against the cache
        // target mpv itself sets. Player 2 publishes `bufferedSeconds` (a timeline POSITION, which
        // is why it maps to cacheTime above) and `networkStalled` (a boolean). A percentage needs a
        // target to be a percentage OF, and this engine declares none - so any number here would be
        // a denominator this file made up, moving smoothly and meaning nothing. Returning ""
        // matches what PlayerPage's own guard is written for (PlayerPage.qml:223-225): the branch
        // is skipped and the state line falls through to Paused/Seek/speed.
        // The cost is real and is written down for Task 10: a P2 viewer sees no percentage during a
        // stall where an mpv viewer does.
        case "cache-buffering-state":
            return ""
        // The pause card's quality line asks for two MORE keys, and it asks through mpvClean() with
        // a string literal - so the facade contract's `mpv.` scan is structurally blind to them and
        // the port plan listed twelve where PlayerPage uses fourteen. Found by the warning at the
        // bottom of this function firing on a real run (2026-07-27); the contract now derives its
        // list from PlayerPage instead of carrying a hand-written one.
        //   audio-params/channel-count - the "5.1"/"2.0" badge (channelLabel, PlayerPage.qml:2770).
        //       diagnostics.audioFormat DOES carry a channel count ("48000 Hz / 2 ch / float32")
        //       but it is the WASAPI SINK's OUTPUT format (Player2Session.cpp:280-284) - a 5.1
        //       source downmixed to stereo reads 2 there, and the badge would then call a 5.1 film
        //       "2.0". The SOURCE track's channel count is genuinely absent: DemuxStreamInfo
        //       carries none, which is the same hole that leaves `channels` empty in _mapTracks.
        //   video-params/transfer - the HDR/HLG badge. resolveColorConversion() is HANDED the
        //       AVCOL_TRC value (ColorHdrPolicy.h:42) and collapses it to a boolean `hdrSource`, so
        //       diagnostics.colorConversion can say "HDR tone-mapped to SDR" but can never tell PQ
        //       from HLG - and those are the two different badges PlayerPage prints. Picking one
        //       would be a coin flip wearing a measurement's clothes.
        //       ⚠ ALSO A PRODUCT QUESTION for the arc, not a mapping one: this engine tone-maps HDR
        //       to SDR (there is no passthrough, ColorHdrPolicy.h:21-22), so whether a P2 boot
        //       should show an "HDR" badge over an SDR picture at all is HIS call.
        case "audio-params/channel-count":
        case "video-params/transfer":
            return ""
        }
        // An unknown key is not a value of "". Say so - a new mpv.mpvProperty() in PlayerPage would
        // otherwise read as a permanently blank row with nothing anywhere to explain it. (The
        // facade contract enumerates the keys statically; this catches the one added mid-flight.)
        console.warn("PlayerEngineP2: mpvProperty has no mapping for", String(name))
        return ""
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

    // `pause` is ONE facade property driving TWO different engine commands, and the two do not
    // accept the same states - so there are two gates, each mirroring its own slot exactly.
    //
    // PLAY direction. play() from Idle, Ended or Error is an ILLEGAL transition, and a rejected
    // transition emits errorOccurred(InvalidCommand) (Player2Session.cpp:686-692) - which reaches
    // PlayerPage as a playbackError and starts its recovery ladder over a button press. Everything
    // else is legal: Opening/Buffering/Paused/Recovering all transition to Playing, and Seeking is
    // steered through m_postSeekState instead (Player2Session.cpp:496-499).
    // KNOWN LIMITATION, not a workaround: pressing play once playback has ENDED does nothing here.
    // Ended only accepts Opening / Seeking / Idle, so replay-from-EOF needs a real re-open; mpv
    // restarted the file instead. Left visible for the arc to answer, not papered over.
    //
    // RELATED, and worse (measured 2026-07-27, engine-side, NOT caused by this file): rewinding a
    // clip whose demux has already reached EOF strands playback - the session says Playing and
    // position never moves. It then reaches Ended a SECOND time, so endFile("eof") fires twice and
    // PlayerPage starts Up Next twice (endFiles=[eof|eof]). Reproduces on the 2s av.mkv fixture;
    // does NOT reproduce on real-length media. Task 7 owns verifying the progress/Up Next path.
    function _transportAcceptsPlay() {
        if (!p2.s)
            return false
        var st = p2.s.state
        return st === p2.stOpening || st === p2.stBuffering || st === p2.stPlaying
               || st === p2.stPaused || st === p2.stSeeking || st === p2.stRecovering
    }

    // PAUSE direction - a STRICTLY smaller set, read straight off Player2Session::pause
    // (Player2Session.cpp:509-525): Seeking steers m_postSeekState, Playing and Buffering
    // transition, and EVERY OTHER STATE IS A SILENT `return`.
    // Recovering is the one that matters and it is why this is split rather than shared: during a
    // torrent rebuffer the session sits in Recovering while root.fileReady is still true, so
    // togglePlayPause (PlayerPage.qml:2478-2481) is NOT gated - the viewer presses pause, the
    // button latches, nothing pauses, and when the stall clears the state mirror below sees
    // Playing and un-presses the button by itself. Same path for suspendForMinimize
    // (PlayerPage.qml:2139-2141), which pauses specifically to STOP consuming resources during a
    // minimize. The C++ reasoned about exactly this for Seeking - "a control that looks active and
    // ignores the viewer" - and the old shared gate simply let Recovering through instead.
    // Opening leaves with it: pause there was never accepted either.
    function _transportAcceptsPause() {
        if (!p2.s)
            return false
        var st = p2.s.state
        return st === p2.stPlaying || st === p2.stBuffering || st === p2.stSeeking
    }

    // The engine's own answer for "is it paused". There is no `paused` property on the session -
    // Paused IS a state - so this is the whole of it.
    function _adoptPause() { p2._adopt("pause", p2.s ? (p2.s.state === p2.stPaused) : false) }

    property bool pause: false
    onPauseChanged: {
        if (p2._applying || !p2.s)
            return
        if (p2.pause) {
            if (p2._transportAcceptsPause()) { p2.s.pause(); return }
        } else {
            if (p2._transportAcceptsPlay()) { p2.s.play(); return }
        }
        // The engine cannot take this command in the state it is in, and would drop it in silence.
        // PlayerEngine.qml:55-69's invariant is that the engine either ACCEPTS a pushed value or
        // EMITS a change signal; a dropped write does neither, so the facade has to be walked back
        // here or it holds a pause that is not happening. This is the ONE relay in this file whose
        // engine-side truth is a state rather than a property, which is why it was the one missing
        // an adopt-back. Deliberately NOT reached when the push was accepted: a pause taken during
        // Seeking is honoured through m_postSeekState with no state change at all, and snapping
        // back there would undo the very press the C++ went out of its way to keep.
        p2._adoptPause()
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
        p2._adopt("speed", p2.s.speed)
    }

    // mpv's surface is 0..100 (an int). The session's volume is a linear float 0..1.
    property int volume: 100
    onVolumeChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setVolume(p2.volume / 100)
        p2._adopt("volume", Math.round(p2.s.volume * 100))
    }

    // The session's property is `muted`; mpv's surface member is `mute`. Not the same name.
    property bool mute: false
    onMuteChanged: {
        if (p2._applying || !p2.s)
            return
        p2.s.setMuted(p2.mute)
        p2._adopt("mute", p2.s.muted)
    }

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
                // Cleared HERE and not only in loadFile(): open() calls resetMediaProperties()
                // first, which itself emits tracksChanged (Player2Session.cpp:734-737) - that one
                // must not pre-arm the gate for the open that follows it.
                p2._metadataReady = false
                // The previous file's stream indices mean nothing in the new one. Cleared here so a
                // stale id cannot tick a row of the NEW track list (they are plain integers, so
                // "1" from the last file happily matches a different "1" in this one) - and cleared
                // through _adopt so PlayerPage is told, rather than left holding it.
                p2._appliedAudioTrack = ""
                p2._appliedSubtitleTrack = ""
                p2._adopt("audioTrack", "")
                p2._adopt("subtitleTrack", "")
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
                // _transportAcceptsPlay ABOVE - a stranded rewind reaches Ended twice, so Up Next
                // fires TWICE. Verifying that is Task 7's, not fixed here.
                if (!p2._endedFired) {
                    p2._endedFired = true
                    p2.endFile("eof")       // PlayerPage branches on exactly this string
                }
            } else if (st === p2.stError) {
                if (!p2._endedFired) {
                    p2._endedFired = true
                    // Spent here but NOT in the Ended branch above, and the asymmetry is load
                    // bearing: Error is reachable straight from Opening, i.e. BEFORE the rendezvous
                    // has fired, so the gate has to be closed by hand or a later stray Playing (a
                    // recovery reopen, say) would announce fileLoaded for a playback that failed.
                    // Ended is only reachable from a transport that was already running, which
                    // means the rendezvous has necessarily already spent the gate.
                    p2._awaitingLoad = false
                    p2.endFile("error")     // not "eof": must NOT record progress or start Up Next
                }
            } else {
                // THE single owner of this flag's reset - every non-terminal state passes through
                // here, Opening included, so nothing else needs to clear it. A seek back out of EOF
                // lands here too, which is what lets a rewound file end again.
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
            p2._adopt("audioTrack", p2._appliedAudioTrack)
        }
        // -1 here is a real answer, not an absence: it is what the demux reports when subtitles were
        // turned OFF and when the chosen subtitle decoder failed to open (DemuxSession.cpp:1060-1070).
        // Either way "" is what PlayerPage reads as off, which is what is true.
        function onSubtitleTrackChanged(generation, streamIndex) {
            p2._appliedSubtitleTrack = streamIndex >= 0 ? String(streamIndex) : ""
            p2._adopt("subtitleTrack", p2._appliedSubtitleTrack)
        }
        function onSubDelayChanged() { p2._adopt("subDelay", p2.s.subDelay) }
        function onAudioDelayChanged() { p2._adopt("audioDelay", p2.s.audioDelay) }
        function onSpeedChanged() { p2._adopt("speed", p2.s.speed) }
        function onVolumeChanged() { p2._adopt("volume", Math.round(p2.s.volume * 100)) }
        function onMutedChanged() { p2._adopt("mute", p2.s.muted) }
        function onVideoFillChanged() {
            p2._adopt("panscan", p2.s.panscan)
            p2._adopt("videoZoom", p2.s.videoZoom)
            p2._adopt("videoAspect", p2.s.videoAspect)
            p2.videoFillChanged()
        }
    }
}

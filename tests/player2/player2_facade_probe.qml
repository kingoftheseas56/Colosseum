import QtQuick
import QtQuick.Window
import "../../qml"

// Facade probe (chrome-port Task 3): does PLAYERPAGE - the real chrome, not a rebuilt shell -
// drive a real file through PlayerEngine on a Player 2 boot?
//
// It hosts PlayerPage rather than PlayerEngineP2 directly on purpose. The point of the port is
// that PlayerPage's own handlers stay bound to the same names, so a probe that talked to the inner
// engine straight would prove the one thing never in doubt. Reaching the facade through
// PlayerPage's child list is what proves the object PLAYERPAGE built is the one asserted against.
//
// Usage (from the worktree root):
//   colosseum.exe tests/player2/player2_facade_probe.qml <media> [auto|eof|transport|tracks]
//
// THREE SEQUENCES, because no single clip can carry them all:
//   eof       - runs the clip to its end untouched: fileStarted, ONE fileLoaded, endFile("eof").
//               endFile("eof") is what calls recordProgress() and starts Up Next (PlayerPage.qml:
//               2876-2881), i.e. exactly what Task 7 exists to prove.
//   transport - pause freezes position, seekExact lands, resume advances again, and fileLoaded
//               STILL fired only once (seekExact advances the session's generation, so a
//               generation-keyed fileLoaded would re-fire here and re-apply the resume seek).
//               Needs runway: the built fixture av.mkv is TWO SECONDS
//               (tests/player2/fixtures/make_media_fixtures.ps1:26-31) and its position jumps
//               almost to the end inside one 250ms tick, so there is nothing to measure on it.
//               The seek is measured WHILE PAUSED on purpose. `position` is the demuxed-packet
//               frontier, not the presentation clock (Player2Session.cpp:66-73), so a seek issued
//               while PLAYING reads back ~4s past its target within one tick - the read-ahead,
//               not a miss (measured 2026-07-27: seekExact(7.05) read 11.24 250ms later, then
//               advanced at exactly 1.0x). Paused, the frontier IS the landing point and the same
//               seek lands to the centisecond, so this is the assertion that can actually fail.
//   tracks    - (chrome-port Task 4) the track lists, track SELECTION and the subtitle renderer.
//               Asserts through PLAYERPAGE'S OWN row builders - page.audioRows / page.subRows, not
//               the raw engine lists - because the whole risk here is a shape mismatch that reads
//               as `undefined` inside audioRow()/subtitleRow() and fails silently, exactly as the
//               raw chapter forward did. Then it switches the audio track and enables the subtitle
//               track through PlayerPage's own writers and waits for the DEMUX to report back, so
//               a control that moved without reaching the engine cannot pass.
//               Needs tests/player2/fixtures/make_media_fixtures.ps1's tracks-long.mkv: 60s, two
//               audio tracks (eng "English" / fra "French") and an eng subrip track whose cues sit
//               at 10-13s and 25-28s. NO OTHER FIXTURE WORKS, and the LENGTH is load-bearing twice
//               over: a subtitle track can only be selected once the session reports active media
//               (so a 2s clip's cues are demuxed and gone before anything can arm - an unselected
//               subtitle stream is never decoded), and the demux is only paced by playback while
//               its queues are full, so a short clip is read to the end in a couple of seconds and
//               the frontier laps every cue. A 12s cut of this same fixture lost its first cue on
//               one run in two (measured 2026-07-27).
//   auto      - picks eof for a clip <= 60s, transport otherwise. Run BOTH to cover both.
//
// Environment, all load-bearing:
//   COLOSSEUM_PLAYER2=1  - the RHI is a per-PROCESS boot choice; without it PlayerEngine.p2 is
//                          false and this probe measures mpv.
//   QSG_NO_VSYNC=1       - without it QML Timers stop firing once Player 2 playback starts and
//                          this probe emits NOTHING at all (cost hours, 2026-07-25).
//   PATH must lead with Qt's bin dir, or the exe cannot load its own DLLs.
// The window is visible on purpose: a hidden QQuickWindow never renders, so the video item's
// render-thread initialisation would silently never happen.
//
// The exit code is the verdict: 0 only if every assertion that RAN passed, and any assertion the
// media could not carry is printed as SKIPPED rather than quietly dropped.
Window {
    id: probe
    width: 960
    height: 540
    visible: true
    color: "black"
    title: "PlayerEngine facade probe"

    // arguments: [exe, this.qml, media, mode]
    // REQUIRED, with no default. The obvious default - the built fixture - lives in a SIBLING
    // WORKTREE's build directory, which is one machine's layout and nobody else's; committing it
    // would make this probe pass or fail on where the checkout happens to sit. An absent argument
    // is a setup error and says so.
    readonly property string media: Qt.application.arguments.length > 2
                                    ? Qt.application.arguments[2] : ""
    readonly property string modeArg: Qt.application.arguments.length > 3
                                      ? Qt.application.arguments[3] : "auto"

    property var engine: null
    property string mode: "auto"
    property string phase: "find-engine"
    property int ticks: 0
    property int phaseTicks: 0
    property var failures: []
    property var passes: []
    property var skips: []

    // lifecycle evidence, collected from the facade's own signals
    property int sawFileStarted: 0
    property int sawFileLoaded: 0
    property int sawTrackList: 0
    property var endReasons: []
    property var errors: []

    property real mark: -1
    property real seekTarget: -1

    // ---- tracks sequence state -------------------------------------------------------------
    property string switchAudioTo: ""
    property string subTrackId: ""
    property int armTick: -1              // the tick the subtitle track was armed on
    property int subShowCount: 0
    property int showsAtOff: -1
    property bool subWasShowing: false
    property string subCueSeen: ""
    property real subCueFirstPos: -1
    property bool subCleared: false

    function ok(name, detail) { probe.passes.push(name + (detail ? " (" + detail + ")" : "")) }
    function bad(name, detail) { probe.failures.push(name + (detail ? " (" + detail + ")" : "")) }
    function skip(name, why) { probe.skips.push(name + " - " + why) }

    // PlayerEngine is a direct child of PlayerPage's root Item (PlayerPage.qml:2821). QML ids are
    // not reachable from outside, so identify it by the surface it is contracted to expose.
    function findEngine(item) {
        var kids = item.children
        for (var i = 0; i < kids.length; i++) {
            var k = kids[i]
            if (k && typeof k.seekExact === "function" && k.duration !== undefined
                && k.inner !== undefined)
                return k
        }
        return null
    }

    function sessionState() {
        if (!probe.engine || !probe.engine.inner || !probe.engine.inner.s)
            return -1
        return probe.engine.inner.s.state
    }

    function diag() {
        return "state=" + probe.sessionState()
             + " pos=" + (probe.engine ? probe.engine.position.toFixed(2) : "?")
             + " dur=" + (probe.engine ? probe.engine.duration.toFixed(2) : "?")
             + " pause=" + (probe.engine ? probe.engine.pause : "?")
             + " seeking=" + (probe.engine ? probe.engine.coreSeeking : "?")
    }

    function toPhase(next) { probe.phase = next; probe.phaseTicks = 0 }

    property real stopMark: -1
    function armStop() {
        console.log("FACADE PROBE: closing the player via PlayerPage.stop() " + probe.diag())
        page.stop()
        probe.stopMark = -1
        probe.toPhase("stop")
    }

    // Chapters are the one forwarded list whose SHAPE PlayerPage silently depends on: it reads
    // `startSec` and nothing else, and an undefined there does not throw - it makes
    // chapterAtFraction's loop never break (so every lookup names the LAST chapter), erases every
    // seek-bar notch, and zeroes SkipSegments' chapter starts. So this asserts through PlayerPage's
    // OWN chapterAtFraction, not through the raw list: the wrong shape has to fail here.
    // Needs a chaptered file - the built fixture chaptered.mkv has "First" at 0s and "Second" at 1s
    // (tests/player2/fixtures/make_media_fixtures.ps1:54-71).
    function checkChapters() {
        var list = probe.engine.chapters || []
        if (list.length < 2) {
            probe.skip("chapter shape + chapterAtFraction",
                       "media has " + list.length + " chapter(s); run against the chaptered.mkv fixture")
            return
        }
        var shapeOk = true
        for (var i = 0; i < list.length; i++) {
            if (typeof list[i].startSec !== "number" || isNaN(list[i].startSec))
                shapeOk = false
        }
        if (shapeOk)
            probe.ok("chapter rows carry a numeric startSec", "n=" + list.length)
        else
            probe.bad("chapter rows carry a numeric startSec",
                      "PlayerPage reads .startSec; got " + JSON.stringify(list[0]))

        var first = page.chapterAtFraction(0.0)
        if (first.idx === 0)
            probe.ok("chapterAtFraction(0) names the FIRST chapter",
                     "idx=0 title=" + first.title)
        else
            probe.bad("chapterAtFraction(0) names the FIRST chapter",
                      "got " + JSON.stringify(first) + " - the loop never broke")

        // ...and the last one still resolves, so a mapping that simply zeroed everything would not
        // sneak past the check above.
        var lastStart = Number(list[list.length - 1].startSec || 0)
        var last = page.chapterAtFraction(lastStart + 0.01)
        if (last.idx === list.length - 1)
            probe.ok("chapterAtFraction(lastStart) names the LAST chapter",
                     "idx=" + last.idx + " title=" + last.title)
        else
            probe.bad("chapterAtFraction(lastStart) names the LAST chapter",
                      "got " + JSON.stringify(last))
        console.log("FACADE PROBE: chapters=" + JSON.stringify(list))
    }

    // ---- tracks sequence ----------------------------------------------------------------------
    function rowById(rows, id) {
        for (var i = 0; i < (rows || []).length; i++)
            if (String(rows[i].id) === String(id))
                return rows[i]
        return null
    }

    function field(label, row, key, want) {
        var got = row[key]
        if (String(got) === String(want))
            probe.ok(label + "." + key, String(got))
        else
            probe.bad(label + "." + key, "got " + JSON.stringify(got) + " want " + JSON.stringify(want))
    }

    // Everything here reads PLAYERPAGE'S rows, never the engine's list. audioRow()/subtitleRow()
    // are where a shape mismatch actually bites: they read `id`, `lang`, `title`, `codec`,
    // `default`, `forced`, `selected` off the raw engine row and quietly return undefined-derived
    // junk for any key that is named differently, which is precisely how the chapter forward failed.
    function checkTrackShape() {
        var a = page.audioRows || []
        if (a.length !== 2) {
            probe.bad("audioRows has both fixture tracks", "n=" + a.length
                      + " - run against tracks-long.mkv")
            return false
        }
        probe.ok("audioRows has both fixture tracks", "n=2")
        var eng = probe.rowById(a, "1")
        var fra = probe.rowById(a, "2")
        if (!eng || !fra) {
            probe.bad("audio rows carry the stream index as `id`",
                      "ids=" + JSON.stringify(a.map(function(r) { return r.id })))
            return false
        }
        probe.ok("audio rows carry the stream index as `id`", "ids=1,2")
        // `label`, not `title`: audioRow() emits no title key at all (PlayerPage.qml:414-427) - it
        // folds title/lang into `label`. Asserting a `title` here failed on the first run, and the
        // mapping was right; the assertion was wrong. This is the exact reason these checks read
        // PlayerPage's builders instead of the engine's list.
        probe.field("audioRow(eng)", eng, "lang", "eng")
        probe.field("audioRow(eng)", eng, "label", "English")
        probe.field("audioRow(eng)", eng, "codec", "aac")
        probe.field("audioRow(eng)", eng, "default", true)
        probe.field("audioRow(fra)", fra, "lang", "fra")
        probe.field("audioRow(fra)", fra, "label", "French")
        probe.field("audioRow(fra)", fra, "default", false)
        // trackTech() is built from the raw row too - codec only on this branch, because
        // DemuxStreamInfo carries no channel count and no bitrate. "AAC · " would mean a mapping
        // invented empty strings that read as present.
        probe.field("audioRow(eng)", eng, "tech", "AAC")

        // Exactly one tick, and it must be the track the ENGINE says is decoding - the demux reports
        // that unprompted at open, so a `selected` derived from anything else shows here.
        var sel = a.filter(function(r) { return r.selected })
        if (sel.length === 1)
            probe.ok("exactly one audio row is selected", "id=" + sel[0].id)
        else
            probe.bad("exactly one audio row is selected", "n=" + sel.length)
        if (sel.length === 1 && String(sel[0].id) === String(probe.engine.audioTrack)
                && String(probe.engine.audioTrack).length > 0)
            probe.ok("selected audio row matches the engine's reported track",
                     "audioTrack=" + probe.engine.audioTrack)
        else
            probe.bad("selected audio row matches the engine's reported track",
                      "audioTrack=" + JSON.stringify(probe.engine.audioTrack))
        var selRow = page.selectedAudioRow()
        if (selRow)
            probe.ok("PlayerPage.selectedAudioRow() resolves", "id=" + selRow.id)
        else
            probe.bad("PlayerPage.selectedAudioRow() resolves",
                      "null - the chip and the saved-track pref both read this")
        if (page.audioChipValue === "ENG")
            probe.ok("audio chip reads the track language", page.audioChipValue)
        else
            probe.bad("audio chip reads the track language", "got " + page.audioChipValue)

        var sub = probe.rowById(page.subRows, "3")
        if (!sub) {
            probe.bad("subRows carries the embedded subtitle track",
                      "ids=" + JSON.stringify((page.subRows || []).map(function(r) { return r.id })))
            return false
        }
        probe.ok("subRows carries the embedded subtitle track", "id=3")
        probe.subTrackId = "3"
        probe.field("subtitleRow", sub, "lang", "eng")
        probe.field("subtitleRow", sub, "title", "English subs")
        probe.field("subtitleRow", sub, "label", "English subs")
        probe.field("subtitleRow", sub, "codec", "subrip")
        probe.field("subtitleRow", sub, "external", false)
        probe.field("subtitleRow", sub, "forced", false)
        probe.field("subtitleRow", sub, "tech", "SUBRIP · embedded")
        // NOT asserted as false: PlayerPage's own maybeAutoSelectTracks() may already have picked
        // this track by language before the probe got a look in (it does, on a default profile -
        // measured 2026-07-27, which is itself proof that the automation reaches this engine). What
        // must hold either way is that the tick agrees with the track the ENGINE says is decoding.
        if (!!sub.selected === (String(sub.id) === String(probe.engine.subtitleTrack)))
            probe.ok("subtitle row tick agrees with the engine's reported track",
                     "selected=" + sub.selected + " subtitleTrack="
                     + JSON.stringify(probe.engine.subtitleTrack))
        else
            probe.bad("subtitle row tick agrees with the engine's reported track",
                      "selected=" + sub.selected + " subtitleTrack="
                      + JSON.stringify(probe.engine.subtitleTrack))
        console.log("FACADE PROBE: PlayerPage's own automation left subtitleTrack="
                    + JSON.stringify(probe.engine.subtitleTrack))

        probe.switchAudioTo = String(probe.engine.audioTrack) === "1" ? "2" : "1"
        return true
    }

    // The delays have no visible surface to read back through, so they are asserted against the
    // SESSION's own value - the engine's, not the facade's echo of what was pushed.
    function checkDelays() {
        probe.engine.subDelay = 0.25
        probe.engine.audioDelay = -0.5
        var s = probe.engine.inner.s
        if (Math.abs(s.subDelay - 0.25) < 1e-9)
            probe.ok("subDelay reaches the session", "s.subDelay=" + s.subDelay)
        else
            probe.bad("subDelay reaches the session", "s.subDelay=" + s.subDelay)
        if (Math.abs(s.audioDelay + 0.5) < 1e-9)
            probe.ok("audioDelay reaches the session", "s.audioDelay=" + s.audioDelay)
        else
            probe.bad("audioDelay reaches the session", "s.audioDelay=" + s.audioDelay)
        // ...and the facade did not drift off the value the engine actually took.
        if (probe.engine.subDelay === s.subDelay && probe.engine.audioDelay === s.audioDelay)
            probe.ok("facade delays match the engine after the push")
        else
            probe.bad("facade delays match the engine after the push",
                      "facade=" + probe.engine.subDelay + "/" + probe.engine.audioDelay)
        probe.engine.subDelay = 0
        probe.engine.audioDelay = 0
    }

    // SubStyleBar hands the ENGINE each of its keys as a string (SubStyleBar.qml:52-58), so a
    // mistyped case in the branch's setSubOption() is a control that moves and does nothing, with no
    // error anywhere - the exact failure this port exists to kill. Driven through the facade's own
    // forwarder, and read back off the inner engine's style properties, which are what the QML
    // subtitle renderer binds to. The RENDERING itself is not asserted here: it is a QML paint on an
    // uncapturable D3D surface, so it stays owed to a human's eyes.
    function checkSubStyle() {
        var inner = probe.engine.inner
        var before = { "subScale": inner.subScale, "subColor": String(inner.subColor),
                       "subBorderSize": inner.subBorderSize,
                       "subBorderColor": String(inner.subBorderColor), "subPos": inner.subPos }
        var cases = [["sub-scale", 1.5, "subScale", 1.5],
                     ["sub-color", "#f0c44a", "subColor", "#f0c44a"],
                     ["sub-border-size", 4.5, "subBorderSize", 4.5],
                     ["sub-border-color", "#20314a", "subBorderColor", "#20314a"],
                     ["sub-pos", 40, "subPos", 40]]
        for (var i = 0; i < cases.length; i++) {
            var key = cases[i][0], prop = cases[i][2]
            probe.engine.setSubOption(key, cases[i][1])
            var got = inner[prop]
            var want = cases[i][3]
            var same = (typeof want === "string")
                     ? String(got).toLowerCase() === String(want).toLowerCase()
                     : Number(got) === Number(want)
            if (same)
                probe.ok("setSubOption('" + key + "') reaches the engine", prop + "=" + got)
            else
                probe.bad("setSubOption('" + key + "') reaches the engine",
                          prop + "=" + got + " want " + want)
        }
        // The one key with nothing to implement must be ABSENT from the bar, not inert in it.
        if (probe.engine.supportsSubAssOverride === false)
            probe.ok("sub-ass-override is capability-gated off on Player 2")
        else
            probe.bad("sub-ass-override is capability-gated off on Player 2",
                      "supportsSubAssOverride=" + probe.engine.supportsSubAssOverride)
        if (probe.engine.supportsExternalSubs === false)
            probe.ok("external subtitles are capability-gated off on Player 2")
        else
            probe.bad("external subtitles are capability-gated off on Player 2",
                      "supportsExternalSubs=" + probe.engine.supportsExternalSubs)
        for (var k in before)
            probe.engine.setSubOption(k === "subScale" ? "sub-scale"
                                    : k === "subColor" ? "sub-color"
                                    : k === "subBorderSize" ? "sub-border-size"
                                    : k === "subBorderColor" ? "sub-border-color" : "sub-pos",
                                      before[k])
    }

    // Sampled every tick of the tracks sequence. The transitions - not a single reading - are the
    // evidence: a cue that appears and never clears is as wrong as one that never appears.
    function sampleSubtitle() {
        if (!probe.engine || !probe.engine.inner || !probe.engine.inner.s)
            return
        var text = String(probe.engine.inner.s.subtitleText || "")
        var showing = text.length > 0
        if (showing && !probe.subWasShowing) {
            probe.subShowCount += 1
            if (probe.subCueFirstPos < 0) {
                probe.subCueFirstPos = probe.engine.position
                probe.subCueSeen = text
            }
            console.log("FACADE PROBE: subtitle SHOW \"" + text + "\" pos="
                        + probe.engine.position.toFixed(2))
        } else if (!showing && probe.subWasShowing) {
            probe.subCleared = true
            console.log("FACADE PROBE: subtitle CLEAR pos=" + probe.engine.position.toFixed(2))
        }
        probe.subWasShowing = showing
    }

    function reportTracks() {
        if (probe.subShowCount >= 1)
            probe.ok("session.subtitleText becomes non-empty inside the cue window",
                     "\"" + probe.subCueSeen + "\" at pos=" + probe.subCueFirstPos.toFixed(2))
        else
            probe.bad("session.subtitleText becomes non-empty inside the cue window",
                      "never - subtitleTrack=" + JSON.stringify(probe.engine.subtitleTrack))
        if (probe.subCueSeen === "Player 2 subtitle fixture")
            probe.ok("the painted cue is the fixture's own text")
        else
            probe.bad("the painted cue is the fixture's own text",
                      "got " + JSON.stringify(probe.subCueSeen))
        // One-sided by construction, and honest about it: `position` is the DEMUX FRONTIER, which is
        // always >= the playback clock, so a cue painted before its 10s window must read < 10.0 here
        // while a correctly-clocked one cannot. It catches a display-on-arrival regression (cues
        // decode seconds ahead); it cannot catch a small late paint.
        if (probe.subCueFirstPos >= 10.0)
            probe.ok("the cue was not painted before its window opened",
                     "frontier=" + probe.subCueFirstPos.toFixed(2))
        else
            probe.bad("the cue was not painted before its window opened",
                      "frontier=" + probe.subCueFirstPos.toFixed(2) + " < 10.0 (cue starts at 10s)")
        if (probe.subCleared)
            probe.ok("session.subtitleText goes empty again after the cue window")
        else
            probe.bad("session.subtitleText goes empty again after the cue window",
                      "still showing \"" + probe.subCueSeen + "\"")
    }

    // The strongest thing this sequence can say about Off, and it needs the fixture's SECOND cue to
    // say it: subtitles are turned off after the first cue has come and gone, and the clip then runs
    // through the 25-28s cue. If Off had not reached the engine, that cue would paint. Nothing about
    // the facade's own value can fake this - it is measured on the session's published text.
    function reportSubsOff() {
        if (probe.subShowCount === probe.showsAtOff)
            probe.ok("after Off, the fixture's second cue never paints",
                     "shows=" + probe.subShowCount + " (cue 2 is at 25-28s)")
        else
            probe.bad("after Off, the fixture's second cue never paints",
                      "shows went " + probe.showsAtOff + " -> " + probe.subShowCount)
    }

    function finish() {
        console.log("FACADE PROBE: --- passes (" + probe.passes.length + ") ---")
        for (var i = 0; i < probe.passes.length; i++)
            console.log("FACADE PROBE: PASS " + probe.passes[i])
        if (probe.skips.length) {
            console.log("FACADE PROBE: --- skipped (" + probe.skips.length + ") ---")
            for (var s = 0; s < probe.skips.length; s++)
                console.log("FACADE PROBE: SKIP " + probe.skips[s])
        }
        console.log("FACADE PROBE: --- failures (" + probe.failures.length + ") ---")
        for (var j = 0; j < probe.failures.length; j++)
            console.log("FACADE PROBE: FAIL " + probe.failures[j])
        console.log("FACADE PROBE: lifecycle fileStarted=" + probe.sawFileStarted
                    + " fileLoaded=" + probe.sawFileLoaded
                    + " trackListChanged=" + probe.sawTrackList
                    + " endFile=[" + probe.endReasons.join("|") + "]"
                    + " playbackError=[" + probe.errors.join("|") + "]")
        console.log("FACADE PROBE: mode=" + probe.mode
                    + " " + (probe.failures.length ? "RESULT FAIL" : "RESULT PASS"))
        Qt.exit(probe.failures.length ? 1 : 0)
    }

    PlayerPage {
        id: page
        anchors.fill: parent
    }

    Connections {
        target: probe.engine
        // No ignoreUnknownSignals, deliberately - the same choice PlayerEngine.qml:150-153 makes
        // and for the same reason. Every handler below names a signal PlayerEngine declares, so if
        // one is ever renamed this probe must say so out loud instead of quietly watching nothing.
        // A null target (before the engine is found) connects nothing and warns about nothing.
        function onFileStarted() {
            probe.sawFileStarted += 1
            console.log("FACADE PROBE: signal fileStarted " + probe.diag())
        }
        function onFileLoaded() {
            probe.sawFileLoaded += 1
            console.log("FACADE PROBE: signal fileLoaded " + probe.diag())
        }
        function onEndFile(reason) {
            probe.endReasons.push(String(reason))
            console.log("FACADE PROBE: signal endFile reason=" + reason + " " + probe.diag())
        }
        function onPlaybackError(code, message) {
            probe.errors.push(code + ":" + message)
            console.log("FACADE PROBE: signal playbackError " + code + " / " + message)
        }
        function onTrackListChanged() { probe.sawTrackList += 1 }
    }

    Timer {
        interval: 250
        repeat: true
        running: true
        onTriggered: {
            probe.ticks += 1
            probe.phaseTicks += 1
            if (probe.mode === "tracks")
                probe.sampleSubtitle()
            if (probe.ticks % 8 === 0)
                console.log("FACADE PROBE: tick=" + probe.ticks + " phase=" + probe.phase + " " + probe.diag())

            // Hard ceiling, so a stuck engine reports rather than hangs a runner.
            if (probe.ticks > 300) {
                probe.bad("overall timeout", "phase=" + probe.phase + " " + probe.diag())
                probe.finish()
                return
            }

            switch (probe.phase) {

            case "find-engine": {
                if (!probe.media.length) {
                    probe.bad("media argument supplied",
                              "usage: colosseum.exe <this.qml> <media> [auto|eof|transport|tracks]")
                    probe.finish()
                    return
                }
                var e = probe.findEngine(page)
                if (!e) {
                    if (probe.phaseTicks > 8) {
                        probe.bad("PlayerPage built a PlayerEngine", "no child exposing the facade surface")
                        probe.finish()
                    }
                    return
                }
                probe.engine = e
                probe.ok("PlayerPage built a PlayerEngine")
                if (!e.inner) {
                    probe.bad("PlayerEngine loaded an inner engine", "inner is null")
                    probe.finish()
                    return
                }
                probe.ok("PlayerEngine loaded an inner engine")
                if (e.p2)
                    probe.ok("booted on the Player 2 branch", "PlayerEngine.p2=true")
                else
                    probe.bad("booted on the Player 2 branch", "p2=false - set COLOSSEUM_PLAYER2=1")
                if (e.supportsCapture)
                    probe.bad("capture capability is off on Player 2", "supportsCapture=true")
                else
                    probe.ok("capture capability is off on Player 2")
                // A pause the ENGINE cannot take must not stick to the facade. With no media open
                // the session is Idle, where Player2Session::pause() is a silent `return`
                // (Player2Session.cpp:509-525) - the same shape as the reachable case, a pause
                // pressed during a torrent rebuffer while the session sits in Recovering and
                // PlayerPage's own gate (root.fileReady) is wide open. Deterministic here, where
                // Recovering is not; the mechanism under test is identical either way. Without the
                // adopt-back the button latches, nothing pauses, and the state mirror un-presses it
                // by itself when the stall clears.
                // Player 2 only: mpv's `pause` is a plain property it accepts at any time, with no
                // file open, and starts the next load paused - so on that boot holding the value is
                // CORRECT and this would both fail spuriously and leave the transport parked for
                // every assertion after it.
                if (e.p2) {
                    e.pause = true
                    if (!e.pause)
                        probe.ok("a pause the engine refuses does not stick to the facade",
                                 "state=" + probe.sessionState() + " (Idle)")
                    else
                        probe.bad("a pause the engine refuses does not stick to the facade",
                                  "facade holds pause=true while the engine never paused; state="
                                  + probe.sessionState())
                    e.pause = false     // never leave the transport parked, whatever the outcome
                } else {
                    probe.skip("a pause the engine refuses does not stick to the facade",
                               "mpv accepts a pause with no file open - the refusal path is Player 2's")
                }

                console.log("FACADE PROBE: opening " + probe.media)
                page.playLocalFile({ "id": "probe:facade", "title": "facade probe",
                                     "localPath": probe.media })
                probe.toPhase("advance")
                return
            }

            case "advance": {
                if (probe.engine.position > 0.15) {
                    probe.ok("position advances under PlayerPage",
                             "pos=" + probe.engine.position.toFixed(2))
                    if (probe.engine.duration > 0)
                        probe.ok("duration is known", "dur=" + probe.engine.duration.toFixed(2))
                    else
                        probe.bad("duration is known", "duration=0")
                    if (String(probe.engine.currentUrl).length > 0)
                        probe.ok("currentUrl is published", String(probe.engine.currentUrl))
                    else
                        probe.bad("currentUrl is published", "empty - seek thumbnails key off it")
                    probe.checkChapters()

                    probe.mode = probe.modeArg !== "auto" ? probe.modeArg
                               : (probe.engine.duration <= 60 ? "eof" : "transport")
                    console.log("FACADE PROBE: sequence=" + probe.mode)
                    if (probe.mode === "tracks") {
                        probe.skip("seek / pause-freeze / resume-advance",
                                   "sequence 'tracks' leaves the transport alone; run mode 'transport'")
                        probe.skip("endFile(\"eof\")",
                                   "sequence 'tracks' reports before the end; run mode 'eof' on a short clip")
                        probe.skip("stop() teardown",
                                   "sequence 'tracks' must not close the player; run mode 'transport'")
                        if (!probe.checkTrackShape()) {
                            probe.toPhase("report")
                            return
                        }
                        probe.checkDelays()
                        probe.checkSubStyle()
                        // PlayerPage's OWN writer, and its own automation latch: without
                        // userTouchedAudio a later automation pass would silently put the preferred
                        // language back and the switch would look like it never took.
                        page.userTouchedAudio = true
                        console.log("FACADE PROBE: switching audio to " + probe.switchAudioTo)
                        probe.engine.audioTrack = probe.switchAudioTo
                        probe.toPhase("audio-switch")
                        return
                    }
                    if (probe.mode === "eof") {
                        probe.skip("seek / pause-freeze / resume-advance",
                                   "sequence 'eof' leaves the clip untouched; run mode 'transport' on media > 60s")
                        probe.skip("stop() teardown",
                                   "sequence 'eof' watches the clip end on its own; run mode 'transport'")
                        probe.toPhase("eof")
                    } else {
                        probe.skip("endFile(\"eof\")",
                                   "sequence 'transport' never reaches the end; run mode 'eof' on a short clip")
                        probe.engine.pause = true
                        probe.mark = -1
                        probe.toPhase("freeze")
                    }
                    return
                }
                if (probe.phaseTicks > 80) {
                    probe.bad("position advances under PlayerPage", "never advanced; " + probe.diag())
                    probe.finish()
                }
                return
            }

            // ---- sequence: eof ----------------------------------------------------------------
            case "eof": {
                if (probe.endReasons.length) {
                    if (probe.endReasons.indexOf("eof") >= 0)
                        probe.ok("endFile(\"eof\") fires at end of file")
                    else
                        probe.bad("endFile(\"eof\") fires at end of file",
                                  "got [" + probe.endReasons.join("|") + "]")
                    probe.toPhase("report")
                    return
                }
                if (probe.phaseTicks > 100) {
                    probe.bad("endFile(\"eof\") fires at end of file", "never fired; " + probe.diag())
                    probe.toPhase("report")
                }
                return
            }

            // ---- sequence: tracks -------------------------------------------------------------
            case "audio-switch": {
                // The pass condition is the DEMUX's report, not our own push: audioTrack only holds
                // what audioTrackChanged(gen, streamIndex) said is decoding now, so a select that
                // never reached the engine cannot satisfy this.
                if (String(probe.engine.audioTrack) === probe.switchAudioTo) {
                    probe.ok("audio track switch reaches the engine and is reported back",
                             "audioTrack=" + probe.engine.audioTrack)
                    var row = probe.rowById(page.audioRows, probe.switchAudioTo)
                    if (row && row.selected)
                        probe.ok("the tick moves with it in PlayerPage's audioRows",
                                 "selected id=" + row.id)
                    else
                        probe.bad("the tick moves with it in PlayerPage's audioRows",
                                  "row=" + JSON.stringify(row))
                    // Latch PlayerPage's automation so it cannot re-pick underneath the assertions,
                    // then make sure the track is on. If its own language automation already chose
                    // it, this is a no-op and the wait below passes on the automation's work -
                    // which is the honest reading either way.
                    page.userTouchedSubs = true
                    probe.armTick = probe.ticks
                    console.log("FACADE PROBE: enabling subtitle track " + probe.subTrackId)
                    // PlayerPage's own subtitle router (PlayerPage.qml:299-313) - the same call its
                    // menu makes - so the assignment path under test is the shipped one.
                    page.pickSubtitle(probe.subTrackId)
                    probe.toPhase("subs-arm")
                    return
                }
                if (probe.phaseTicks > 24) {
                    probe.bad("audio track switch reaches the engine and is reported back",
                              "wanted " + probe.switchAudioTo + ", engine still says "
                              + JSON.stringify(probe.engine.audioTrack))
                    probe.toPhase("report")
                }
                return
            }

            case "subs-arm": {
                if (String(probe.engine.subtitleTrack) === probe.subTrackId) {
                    probe.ok("subtitle track selection reaches the engine and is reported back",
                             "subtitleTrack=" + probe.engine.subtitleTrack)
                    var srow = probe.rowById(page.subRows, probe.subTrackId)
                    if (srow && srow.selected)
                        probe.ok("the tick moves with it in PlayerPage's subRows", "selected id=" + srow.id)
                    else
                        probe.bad("the tick moves with it in PlayerPage's subRows",
                                  "row=" + JSON.stringify(srow))
                    if (page.subsChipValue === "ENG")
                        probe.ok("subs chip reads the track language", page.subsChipValue)
                    else
                        probe.bad("subs chip reads the track language", "got " + page.subsChipValue)
                    probe.toPhase("subs-watch")
                    return
                }
                if (probe.phaseTicks > 24) {
                    probe.bad("subtitle track selection reaches the engine and is reported back",
                              "wanted " + probe.subTrackId + ", engine still says "
                              + JSON.stringify(probe.engine.subtitleTrack))
                    probe.toPhase("report")
                }
                return
            }

            case "subs-watch": {
                // Wait for the FIRST cue to come AND go - sampleSubtitle() has been recording every
                // tick since before arming - then, with a whole cue observed and the second one
                // still ahead at 25-28s, turn subtitles off and let the clip run into it.
                if ((probe.subShowCount >= 1 && probe.subCleared) || probe.phaseTicks > 140) {
                    probe.reportTracks()
                    probe.showsAtOff = probe.subShowCount
                    console.log("FACADE PROBE: turning subtitles OFF at pos="
                                + probe.engine.position.toFixed(2))
                    // Exactly what PlayerPage's turnSubtitlesOff() writes to the engine
                    // (PlayerPage.qml:670-674); the surrounding saveTrackPreference() is skipped on
                    // purpose so a probe run does not leave a subtitles-off preference behind.
                    probe.engine.subtitleTrack = ""
                    probe.toPhase("subs-off")
                }
                return
            }

            case "subs-off": {
                if (String(probe.engine.subtitleTrack) === "") {
                    probe.ok("Off reaches the engine and is reported back",
                             "subtitleTrack=\"\" (the session's own -1)")
                    var offRow = probe.rowById(page.subRows, probe.subTrackId)
                    if (offRow && !offRow.selected)
                        probe.ok("the tick clears in PlayerPage's subRows")
                    else
                        probe.bad("the tick clears in PlayerPage's subRows",
                                  "row=" + JSON.stringify(offRow))
                    if (page.subsChipValue === "OFF")
                        probe.ok("subs chip reads OFF", page.subsChipValue)
                    else
                        probe.bad("subs chip reads OFF", "got " + page.subsChipValue)
                    probe.toPhase("subs-after-off")
                    return
                }
                if (probe.phaseTicks > 24) {
                    probe.bad("Off reaches the engine and is reported back",
                              "engine still says " + JSON.stringify(probe.engine.subtitleTrack))
                    probe.toPhase("report")
                }
                return
            }

            case "subs-after-off": {
                // 80 ticks = 20s of wall clock, and playback runs at 1.0x, so this carries the clock
                // from ~13s (where cue 1 ended) well past cue 2's 25-28s window. Bounded by ticks
                // rather than by `position`, which is the demux frontier and says nothing about
                // where the playback clock - the thing that gates a cue - has got to.
                if (probe.endReasons.length || probe.phaseTicks > 80) {
                    probe.reportSubsOff()
                    probe.toPhase("report")
                }
                return
            }

            // ---- sequence: transport ----------------------------------------------------------
            case "freeze": {
                // One tick for the pause to land, then two samples 250ms apart.
                if (probe.phaseTicks < 3)
                    return
                if (probe.mark < 0) {
                    // Asserted against the SESSION's state, not the facade's own property. Reading
                    // back `engine.pause` only proves nobody overwrote what this probe just wrote -
                    // it is true even when the push was dropped in silence, which is exactly the
                    // failure it is supposed to catch. Player2State::Paused is the engine's answer.
                    // mpv has no session to ask, so on that boot the freeze below IS the check.
                    if (probe.engine.p2) {
                        if (probe.sessionState() === 4)
                            probe.ok("pause reaches the engine", "session state=Paused(4)")
                        else
                            probe.bad("pause reaches the engine",
                                      "engine did not pause; " + probe.diag())
                    } else {
                        probe.skip("pause reaches the engine",
                                   "mpv exposes no session state; the freeze assertion is the behavioural check there")
                    }
                    if (!probe.engine.pause)
                        probe.bad("facade agrees it is paused", "facade pause=false; " + probe.diag())
                    probe.mark = probe.engine.position
                    return
                }
                var drift = Math.abs(probe.engine.position - probe.mark)
                if (drift < 0.05)
                    probe.ok("pause freezes position", "drift=" + drift.toFixed(3))
                else
                    probe.bad("pause freezes position", "drift=" + drift.toFixed(3))
                probe.seekTarget = probe.engine.position + 20
                console.log("FACADE PROBE: seeking to " + probe.seekTarget.toFixed(2))
                probe.engine.seekExact(probe.seekTarget)
                probe.toPhase("seek")
                return
            }

            case "seek": {
                // Measured on the FIRST tick after the seek completes, and not one later:
                // seekExact() transitions the session to Seeking synchronously, so coreSeeking is
                // already true when this phase begins. The frontier keeps creeping forward even
                // while paused (the read-ahead does not stop for a pause), so a later sample is a
                // measurement of the buffer, not of the seek.
                if (probe.engine.coreSeeking) {
                    if (probe.phaseTicks > 60) {
                        probe.bad("seekExact completes", "still seeking; " + probe.diag())
                        probe.finish()
                    }
                    return
                }
                // Asymmetric window on purpose: landing BEHIND the target is a real miss, landing a
                // little ahead is the read-ahead frontier this property is made of.
                var delta = probe.engine.position - probe.seekTarget
                if (delta >= -0.5 && delta <= 2.5)
                    probe.ok("seekExact lands", "target=" + probe.seekTarget.toFixed(2)
                             + " pos=" + probe.engine.position.toFixed(2))
                else
                    probe.bad("seekExact lands", "target=" + probe.seekTarget.toFixed(2)
                              + " pos=" + probe.engine.position.toFixed(2))
                probe.mark = probe.engine.position
                probe.engine.pause = false
                probe.toPhase("resume")
                return
            }

            case "resume": {
                if (probe.engine.position > probe.mark + 0.3) {
                    probe.ok("resume advances position again",
                             "from=" + probe.mark.toFixed(2) + " to=" + probe.engine.position.toFixed(2))
                    probe.armStop()
                    return
                }
                if (probe.phaseTicks > 40) {
                    probe.bad("resume advances position again",
                              "stuck at " + probe.mark.toFixed(2) + "; " + probe.diag())
                    probe.toPhase("report")
                }
                return
            }

            // Closing the player must actually END the playback. PlayerPage.stop()
            // (PlayerPage.qml:2124-2133) reaches the engine ONLY through command(["stop"]), and
            // PlayerEngine guards that call on the branch declaring `command` - so a branch without
            // it leaves a closed player with a live session: audio still playing, demux and decode
            // and the pump still running. Driven through PlayerPage.stop(), the real caller
            // (Main.qml:955 closePlayer, Main.qml:1179 close-a-session), and asserted on the
            // SESSION - Idle, and a position that has genuinely stopped moving. "The function was
            // called" would prove nothing here.
            case "stop": {
                if (probe.phaseTicks < 3)
                    return
                if (probe.stopMark < 0) {
                    if (probe.engine.p2) {
                        if (probe.sessionState() === 0)
                            probe.ok("stop() tears the session down", "session state=Idle(0)")
                        else
                            probe.bad("stop() tears the session down",
                                      "session still alive; " + probe.diag())
                    } else {
                        probe.skip("stop() tears the session down",
                                   "mpv exposes no session state; the advance assertion is the behavioural check there")
                    }
                    probe.stopMark = probe.engine.position
                    return
                }
                var moved = Math.abs(probe.engine.position - probe.stopMark)
                if (moved < 0.05)
                    probe.ok("stop() stops playback advancing", "drift=" + moved.toFixed(3))
                else
                    probe.bad("stop() stops playback advancing",
                              "position still moving by " + moved.toFixed(3) + "s; " + probe.diag())
                probe.toPhase("report")
                return
            }

            case "report": {
                if (probe.sawFileStarted > 0)
                    probe.ok("fileStarted fires", "x" + probe.sawFileStarted)
                else
                    probe.bad("fileStarted fires", "never")
                if (probe.sawFileLoaded === 1)
                    probe.ok("fileLoaded fires exactly once", "x1 - a seek must not re-fire it")
                else
                    probe.bad("fileLoaded fires exactly once", "x" + probe.sawFileLoaded)
                if (probe.errors.length === 0)
                    probe.ok("no spurious playbackError")
                else
                    probe.bad("no spurious playbackError", probe.errors.join(" | "))
                probe.finish()
                return
            }
            }
        }
    }
}

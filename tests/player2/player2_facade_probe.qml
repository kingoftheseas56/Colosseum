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
//   colosseum.exe tests/player2/player2_facade_probe.qml <media> [auto|eof|transport]
//
// TWO SEQUENCES, because no single clip can carry both:
//   eof       - runs the clip to its end untouched: fileStarted, ONE fileLoaded, endFile("eof").
//               endFile("eof") is what calls recordProgress() and starts Up Next (PlayerPage.qml:
//               2871-2879), i.e. exactly what Task 7 exists to prove.
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
    readonly property string media: Qt.application.arguments.length > 2
        ? Qt.application.arguments[2]
        : String(Qt.resolvedUrl("../../../player2-task1-isolation/native/build-player2/player2-fixtures/av.mkv"))
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

    function ok(name, detail) { probe.passes.push(name + (detail ? " (" + detail + ")" : "")) }
    function bad(name, detail) { probe.failures.push(name + (detail ? " (" + detail + ")" : "")) }
    function skip(name, why) { probe.skips.push(name + " - " + why) }

    // PlayerEngine is a direct child of PlayerPage's root Item (PlayerPage.qml:2820). QML ids are
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
        ignoreUnknownSignals: true
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
                    if (probe.mode === "eof") {
                        probe.skip("seek / pause-freeze / resume-advance",
                                   "sequence 'eof' leaves the clip untouched; run mode 'transport' on media > 60s")
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

            // ---- sequence: transport ----------------------------------------------------------
            case "freeze": {
                // One tick for the pause to land, then two samples 250ms apart.
                if (probe.phaseTicks < 3)
                    return
                if (probe.mark < 0) {
                    if (probe.engine.pause)
                        probe.ok("pause reaches the engine")
                    else
                        probe.bad("pause reaches the engine", "facade pause still false; " + probe.diag())
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
                    probe.toPhase("report")
                    return
                }
                if (probe.phaseTicks > 40) {
                    probe.bad("resume advances position again",
                              "stuck at " + probe.mark.toFixed(2) + "; " + probe.diag())
                    probe.toPhase("report")
                }
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

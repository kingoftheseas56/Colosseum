// Headless behavioural gate for the drawer's PURE display derivations (Player2Browser.js). Grep
// contracts prove a string is present; this proves the LOGIC — row-state precedence, progress
// clamping, season-pill derivation. Run offscreen: qml.exe -platform offscreen <this file>.
//
// Verdict rides the EXIT CODE (console.log does not flush before exit); an uncaught throw in
// onCompleted HANGS qml.exe, so every assertion lives inside the try/catch that calls Qt.exit.
import QtQml 2.15
import "../../qml/player2/controls/Player2Browser.js" as Browser

QtObject {
    function check(cond, msg) { if (!cond) throw new Error(msg) }
    function close(a, b) { return Math.abs(a - b) < 1e-9 }

    function runChecks() {
        // --- source row state: now beats dead beats playable ---
        check(Browser.sourceRowState(true, false) === "now", "the current source reads now")
        check(Browser.sourceRowState(true, true) === "now", "current wins even if flagged dead")
        check(Browser.sourceRowState(false, true) === "dead", "a dead, non-current source reads dead")
        check(Browser.sourceRowState(false, false) === "playable", "a live source reads playable")

        // --- episode row state: now beats watched beats unwatched; progress clamps to [0,1] ---
        var playing = { mediaId: "s:2:3", progressFraction: 0.5, watched: false }
        check(Browser.episodeRowState(playing, "s:2:3").state === "now",
              "the episode being played reads now")
        var watched = { mediaId: "s:2:1", progressFraction: 1.0, watched: true }
        check(Browser.episodeRowState(watched, "s:2:3").state === "watched",
              "a finished episode reads watched")
        var fresh = { mediaId: "s:2:5", progressFraction: 0.0, watched: false }
        check(Browser.episodeRowState(fresh, "s:2:3").state === "unwatched",
              "an untouched episode reads unwatched")
        check(close(Browser.episodeRowState(playing, "s:2:3").frac, 0.5),
              "progress fraction is carried through")
        check(close(Browser.episodeRowState({ mediaId: "x", progressFraction: 1.7 }, "").frac, 1.0),
              "over-unit progress clamps to 1")
        check(close(Browser.episodeRowState({ mediaId: "x", progressFraction: -3 }, "").frac, 0.0),
              "negative/garbage progress clamps to 0")
        check(Browser.episodeRowState({ mediaId: "" }, "").state === "unwatched",
              "an empty now-id never spuriously marks a row now")

        // --- season pills: 1..count, empty when unknown ---
        var pills = Browser.seasonPills(4)
        check(pills.length === 4 && pills[0] === 1 && pills[3] === 4, "seasons derive as 1..count")
        check(Browser.seasonPills(0).length === 0, "an unknown season count yields no pills")
        check(Browser.seasonPills(1).length === 1, "a single-season show yields one pill")

        // --- skip segments: which segment (if any) contains the playhead; [start,end) half-open ---
        var segs = [
            { kind: "intro", startSeconds: 62, endSeconds: 152, autoSkip: false },
            { kind: "credits", startSeconds: 2520, endSeconds: 2640, autoSkip: true }
        ]
        check(Browser.activeSegment(segs, 30) === null, "before any segment there is nothing to skip")
        check(Browser.activeSegment(segs, 100).kind === "intro", "mid-intro exposes the intro segment")
        check(Browser.activeSegment(segs, 62).kind === "intro", "the segment start is inclusive")
        check(Browser.activeSegment(segs, 152) === null, "the segment end is exclusive (already past it)")
        check(Browser.activeSegment(segs, 200) === null, "between segments there is nothing to skip")
        check(Browser.activeSegment(segs, 2600).kind === "credits", "mid-credits exposes the credits segment")
        check(Browser.activeSegment([], 100) === null, "no segments means nothing to skip")
        check(Browser.activeSegment(null, 100) === null, "a missing segment list is safe")

        // --- skip label reads from the segment kind ---
        check(Browser.skipLabel("intro") === "Skip Intro", "intro labels Skip Intro")
        check(Browser.skipLabel("recap") === "Skip Recap", "recap labels Skip Recap")
        check(Browser.skipLabel("credits") === "Skip Credits", "credits labels Skip Credits")
        check(Browser.skipLabel("weird") === "Skip", "an unknown kind still labels a plain Skip")

        // --- wall clock: 12-hour with AM/PM, zero-padded minutes, 12 at the boundaries ---
        function at(h, m) { return new Date(2026, 0, 1, h, m, 0).getTime() }
        check(Browser.fmtWallClock(at(18, 59)) === "6:59 PM", "evening formats 12-hour PM")
        check(Browser.fmtWallClock(at(6, 5)) === "6:05 AM", "morning zero-pads the minutes")
        check(Browser.fmtWallClock(at(0, 0)) === "12:00 AM", "midnight is 12 AM")
        check(Browser.fmtWallClock(at(12, 0)) === "12:00 PM", "noon is 12 PM")

        // --- ends-at: now + remaining/speed, empty when duration is unknown ---
        check(Browser.endsAtLabel(at(18, 0), 0, 3540, 1) === "6:59 PM", "ends = now + full runtime")
        check(Browser.endsAtLabel(at(18, 0), 300, 3600, 1) === "6:55 PM", "remaining counts from position")
        check(Browser.endsAtLabel(at(18, 0), 0, 3600, 2) === "6:30 PM", "double speed halves the remaining")
        check(Browser.endsAtLabel(at(18, 0), 0, 0, 1) === "", "unknown duration yields no ends time")
        check(Browser.endsAtLabel(at(18, 0), 5000, 3600, 1) === "6:00 PM", "past the end never goes negative")

        // --- pause card: S/E from the episode id, runtime, codec quality line ---
        check(Browser.seasonEpisodeLabel("series-mid:2:3") === "S2 · E3", "episode id yields S/E")
        check(Browser.seasonEpisodeLabel("tt0306414:1:1") === "S1 · E1", "imdb episode id yields S/E")
        check(Browser.seasonEpisodeLabel("tt0306414") === "", "a movie id has no S/E")
        check(Browser.seasonEpisodeLabel("") === "", "an empty id has no S/E")
        check(Browser.seasonEpisodeLabel("a:b:c") === "", "non-numeric tail is not an S/E")

        check(Browser.runtimeLabel(3120) === "52 min", "runtime rounds to whole minutes")
        check(Browser.runtimeLabel(0) === "", "unknown runtime is blank")
        check(Browser.runtimeLabel(-5) === "", "garbage runtime is blank")

        var tracks = [
            { type: "video", codec: "h264 high" }, { type: "audio", codec: "ac3" },
            { type: "subtitle", codec: "subrip" }
        ]
        check(Browser.codecQualityLine(tracks) === "H264 · AC3", "quality line joins video+audio codec, upper")
        check(Browser.codecQualityLine([{ type: "video", codec: "hevc" }]) === "HEVC", "video-only quality line")
        check(Browser.codecQualityLine([]) === "", "no tracks means no quality line")

        // --- PGS bitmap layout: map a cue's canvas-space rect onto the displayed video area ---
        var cue = { x: 400, y: 980, width: 1120, height: 80, canvasWidth: 1920, canvasHeight: 1080 }
        var box = Browser.subtitleBitmapLayout(cue, 1280, 720)   // 2/3 scale on both axes
        check(close(box.x, 400 * 2 / 3), "bitmap x scales by layer/canvas width")
        check(close(box.y, 980 * 2 / 3), "bitmap y scales by layer/canvas height")
        check(close(box.width, 1120 * 2 / 3), "bitmap width scales with the frame")
        check(close(box.height, 80 * 2 / 3), "bitmap height scales with the frame")
        var none = Browser.subtitleBitmapLayout({ x: 0, y: 0, width: 0, height: 0, canvasWidth: 0, canvasHeight: 0 }, 1280, 720)
        check(none.width === 0 && none.height === 0, "a canvas-less cue lays out empty, never divides by zero")

        // --- progress report cadence: report on first tick, every gap seconds, and after a backward seek ---
        check(Browser.shouldReportProgress(-1, 10, 5) === true, "report the very first position")
        check(Browser.shouldReportProgress(10, 12, 5) === false, "hold within the gap")
        check(Browser.shouldReportProgress(10, 15, 5) === true, "report once a full gap has passed")
        check(Browser.shouldReportProgress(10, 30, 5) === true, "report after more than a gap")
        check(Browser.shouldReportProgress(100, 40, 5) === true, "a backward seek reports the new position")
    }

    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.warn("player2_browser_logic: FAIL: " + e.message); Qt.exit(2) }
    }
}

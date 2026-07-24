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
    }

    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.warn("player2_browser_logic: FAIL: " + e.message); Qt.exit(2) }
    }
}

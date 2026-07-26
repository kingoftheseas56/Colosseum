// Player2Browser — the drawer's PURE display derivations. No QML, no context properties, no host
// calls: it turns host-resolved data (episodes, sources, a season count) into the small render states
// the drawer paints. Kept pure so it is driven headless by player2_browser_logic_harness.qml.
//
// Doctrine: the player never searches/ranks/persists — the HOST owns "current source", per-episode
// progress and watched state. This module only classifies what the host already resolved; the one
// thing it derives locally is "now-playing", which is the shell's business (compare media ids).
.pragma library

// Which of the ranked alternate sources is this row? now (playing) beats dead beats playable, so a
// source that is both current and stale still reads as the one you're on.
function sourceRowState(isCurrent, isDead) {
    if (isCurrent) return "now"
    if (isDead) return "dead"
    return "playable"
}

function clampFraction(value) {
    var f = Number(value)
    if (!isFinite(f) || f < 0) return 0
    if (f > 1) return 1
    return f
}

// Row state for one episode against what the shell currently plays. now beats watched beats
// unwatched; frac is the host's progress, clamped for the bar. An empty nowMediaId never marks a row.
function episodeRowState(episode, nowMediaId) {
    var frac = clampFraction(episode ? episode.progressFraction : 0)
    var state
    if (nowMediaId && episode && episode.mediaId === nowMediaId)
        state = "now"
    else if (episode && episode.watched === true)
        state = "watched"
    else
        state = "unwatched"
    return { state: state, frac: frac }
}

// Season pills from the host's season count: [1..count], empty when the count is unknown (<=0).
function seasonPills(seasonCount) {
    var n = Number(seasonCount)
    if (!isFinite(n) || n < 1) return []
    var pills = []
    for (var s = 1; s <= n; ++s) pills.push(s)
    return pills
}

// Which host-resolved skip segment (intro/recap/credits) contains the playhead, or null. The window
// is half-open [start, end): the start is skippable, the end is already past it, so the button clears
// exactly when the seek would land. A missing/empty list is safe (nothing to skip).
function activeSegment(segments, positionSeconds) {
    if (!segments || !segments.length) return null
    var pos = Number(positionSeconds)
    if (!isFinite(pos)) return null
    for (var i = 0; i < segments.length; ++i) {
        var seg = segments[i]
        if (pos >= Number(seg.startSeconds) && pos < Number(seg.endSeconds))
            return seg
    }
    return null
}

// The skip button's label for a segment kind.
function skipLabel(kind) {
    if (kind === "intro") return "Skip Intro"
    if (kind === "recap") return "Skip Recap"
    if (kind === "credits") return "Skip Credits"
    return "Skip"
}

// A 12-hour wall clock ("6:59 PM") from epoch milliseconds. Local time; 12 at the AM/PM boundaries.
function fmtWallClock(epochMs) {
    var d = new Date(epochMs)
    var h = d.getHours(), m = d.getMinutes()
    var ap = h >= 12 ? "PM" : "AM"
    var h12 = h % 12
    if (h12 === 0) h12 = 12
    return h12 + ":" + (m < 10 ? "0" : "") + m + " " + ap
}

// The wall-clock time the media finishes: now + remaining/speed. Empty when the duration is unknown
// (live/no-length). Never negative once you're past the end. Deterministic — the caller passes the
// clock in (nowMs), so this is testable without reading the machine clock.
function endsAtLabel(nowMs, positionSeconds, durationSeconds, speed) {
    var dur = Number(durationSeconds)
    if (!isFinite(dur) || dur <= 0)
        return ""
    var rate = (isFinite(Number(speed)) && Number(speed) > 0.05) ? Number(speed) : 1
    var remaining = Math.max(0, (dur - Number(positionSeconds)) / rate)
    return fmtWallClock(nowMs + remaining * 1000)
}

// "S2 · E3" from a "root:season:episode" media id, or "" when the id has no episode shape (a movie).
function seasonEpisodeLabel(mediaId) {
    var parts = String(mediaId || "").split(":")
    if (parts.length < 3)
        return ""
    var s = parts[parts.length - 2], e = parts[parts.length - 1]
    if (!/^\d+$/.test(s) || !/^\d+$/.test(e))
        return ""
    return "S" + Number(s) + " · E" + Number(e)
}

// "52 min" from a duration in seconds, blank when the runtime is unknown.
function runtimeLabel(durationSeconds) {
    var d = Number(durationSeconds)
    if (!isFinite(d) || d <= 0)
        return ""
    return Math.round(d / 60) + " min"
}

// Should the player report playback progress to the host now? Report the first position, then no more
// than once per `minGapSeconds`, and always right after a backward seek (the position jumped back).
function shouldReportProgress(lastReportedSeconds, nowSeconds, minGapSeconds) {
    var last = Number(lastReportedSeconds), now = Number(nowSeconds)
    if (!isFinite(last) || last < 0)
        return true
    if (now < last)
        return true
    return (now - last) >= Number(minGapSeconds)
}

// Where a PGS/bitmap subtitle cue lands on the displayed video. The cue's x/y/width/height are in the
// subtitle canvas (the source video frame); this scales them onto the layer, which is assumed to hold
// the video content. Returns zeros for a canvas-less cue (never divides by zero). Letterboxed video is
// a later refinement — v1 assumes the video fills the layer.
function subtitleBitmapLayout(cue, layerWidth, layerHeight) {
    var cw = Number(cue ? cue.canvasWidth : 0), ch = Number(cue ? cue.canvasHeight : 0)
    if (!isFinite(cw) || !isFinite(ch) || cw <= 0 || ch <= 0)
        return { x: 0, y: 0, width: 0, height: 0 }
    var sx = Number(layerWidth) / cw, sy = Number(layerHeight) / ch
    return {
        x: Number(cue.x) * sx,
        y: Number(cue.y) * sy,
        width: Number(cue.width) * sx,
        height: Number(cue.height) * sy
    }
}

// The pause card's quality line from the session's tracks: the video codec then the audio codec, first
// token, upper-cased ("H264 · AC3"). Resolution/HDR/channels aren't in Player 2's track shape yet, so
// this is codec-only for now (engine exposure is a follow-up). Blank when there are no tracks.
function codecQualityLine(tracks) {
    if (!tracks || !tracks.length)
        return ""
    function codecOf(kind) {
        for (var i = 0; i < tracks.length; ++i) {
            if (tracks[i].type === kind && tracks[i].codec) {
                var token = String(tracks[i].codec).split(" ")[0]
                if (token.length)
                    return token.toUpperCase()
            }
        }
        return ""
    }
    var out = []
    var v = codecOf("video"); if (v.length) out.push(v)
    var a = codecOf("audio"); if (a.length) out.push(a)
    return out.join(" · ")
}

// Whether closing the player should first ask to confirm. Current-player parity: P1 prompts only when
// actually PLAYING (`fileReady && !mpv.pause`) — a PAUSED close exits immediately (the viewer already
// stopped watching; their spot is saved). So: Buffering(2), Playing(3), Seeking(5), Recovering(7)
// prompt; Idle(0)/Opening(1)/Paused(4)/Ended(6)/Error(8) close without one. Takes the raw Player2State
// enum value (Player2Types.h) so it stays in lockstep with the engine.
function shouldConfirmClose(state) {
    return state === 2 || state === 3 || state === 5 || state === 7
}

// Whether the screensaver / display sleep should be inhibited right now. True only while the picture is
// actively advancing — Buffering(2), Playing(3), Seeking(5), Recovering(7). Released on Paused(4),
// Idle(0), Opening(1), Ended(6) and Error(8), matching the plan's "held while playing, released on
// pause/end/error/close". Takes the raw Player2State enum value so it tracks the engine.
function shouldInhibitSleep(state) {
    return state === 2 || state === 3 || state === 5 || state === 7
}

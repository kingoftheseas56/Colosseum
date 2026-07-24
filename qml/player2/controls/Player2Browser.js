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

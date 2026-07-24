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

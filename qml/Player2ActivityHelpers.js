.pragma library

// Lane B (Theatre Player 2) pure decision logic behind the "Your Colosseum" playback activity
// hook (CPP-PORT-CONTRACT.md §7 Player-2 identity, §9 Lane B). Used by
// qml/player2/Player2Shell.qml. The shared begin/no-op/end state-transition rule
// (ActivityLaneHelpers.decideTransition/keyFor) is CALLED from qml/ActivityLaneHelpers.js —
// never duplicated here — so tests/qml/tst_player2_activity.qml and Lane A/Lane E's own suites
// exercise the exact same transition code every lane actually runs.
//
// This file owns ONLY pure functions (no QObject, no Player2Session/tracker/context-property
// access) so it is trivially unit-testable and safe to `.import` from a QuickTest with no
// component tree.

// §7 Theatre Player 2 identity:
//   titleKey = theatre:<rootMediaId>
//   itemKey  = currentEpisodeId when present, otherwise rootMediaId
//   kind     = episode when currentEpisodeId is present, otherwise movie
//
// rootMediaId empty means the host has not populated a stable identity yet (metadata still
// arriving) -> fail closed to null (§25), the same posture as Lane A's iptv:/local:/arriving:
// placeholder ids failing closed in ActivityLaneHelpers.videoIdentityFor.
function videoIdentityFor(rootMediaId, currentEpisodeId) {
    var root = String(rootMediaId || "")
    if (!root.length)
        return null
    var episodeId = String(currentEpisodeId || "")
    if (episodeId.length)
        return { "kind": "episode", "titleKey": "theatre:" + root, "itemKey": episodeId }
    return { "kind": "movie", "titleKey": "theatre:" + root, "itemKey": root }
}

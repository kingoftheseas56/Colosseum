.pragma library

// Shared pure decision logic behind the "Your Colosseum" playback activity hooks
// (CPP-PORT-CONTRACT.md §7 identity rules, §8 tracker contract, §9 Lane A + Lane E). Used
// verbatim by both qml/PlayerPage.qml (Lane A: Theatre Player 1) and
// qml/AudiobookSession.qml (Lane E: Biblio audiobook) so the begin/no-op/end
// state-transition rule — and its QuickTest coverage in
// tests/qml/tst_player1_activity.qml / tests/qml/tst_audiobook_activity.qml — exercise the
// SAME code the two lanes actually run, not a parallel reimplementation that could drift.
//
// This file owns ONLY pure functions (no QObject, no mpv/tracker/context-property access) so
// it is trivially unit-testable and safe to `.import` from a QuickTest with no component tree.

// Lane A identity derivation (§7 Theatre Player 1). `episodeBrowser` is the imported
// EpisodeBrowser.js module namespace (isEpisodeId/seriesRootId) — passed in rather than
// imported here so this module stays a plain pragma-library with no fixed import path
// coupling (a test can pass its own copy of the same import).
//
// "iptv:" (live TV, not movie/episode), "local:" and "arriving:" (a real id has not landed
// yet — see playLocalFile/playRemoteUrl in PlayerPage.qml) carry no stable cross-session
// identity, so they fail closed to null ("no activity") rather than inventing one (§25).
function videoIdentityFor(mediaId, episodeBrowser) {
    var id = String(mediaId || "")
    if (!id.length)
        return null
    if (id.indexOf("iptv:") === 0 || id.indexOf("local:") === 0 || id.indexOf("arriving:") === 0)
        return null
    if (episodeBrowser.isEpisodeId(id))
        return { "kind": "episode", "titleKey": "theatre:" + episodeBrowser.seriesRootId(id), "itemKey": id }
    return { "kind": "movie", "titleKey": "theatre:" + id, "itemKey": id }
}

// Lane E identity derivation (§7 Audiobook). pairKey is BiblioApi.pairKey(title, author) — a
// normalized text key, not a canonical cross-service book ID (see AudiobookSession.qml) —
// so callers mark the resulting event syncable:false; this module only shapes the identity.
function audiobookIdentityFor(pairKey) {
    var pk = String(pairKey || "")
    if (!pk.length)
        return null
    return { "kind": "audiobook", "titleKey": "biblio:" + pk, "itemKey": pk }
}

// The identity key a lane's activityActiveKey bookkeeping compares against.
function keyFor(identity) {
    return identity ? (identity.kind + "|" + identity.titleKey + "|" + identity.itemKey) : ""
}

// The shared begin/no-op/end state-transition rule — identical for both lanes (§9 Lane A
// "item/episode identity change" reset bullet; Lane E the same, worded for audiobooks):
//   null identity (identityFor returned null)     -> "end"  (harmless no-op if nothing open)
//   identity key matches the currently open one    -> "noop" (a reload/recovery of the SAME
//                                                     item — preserves the 10s gate/session)
//   any other identity (including nothing open yet) -> "begin" (caller ends any open prior
//                                                     session first, then begins the new one)
function decideTransition(currentActiveKey, identity) {
    var newKey = keyFor(identity)
    if (!newKey.length)
        return "end"
    if (newKey === currentActiveKey)
        return "noop"
    return "begin"
}

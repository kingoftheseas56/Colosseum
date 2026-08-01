// ExplicitContentPolicy.js — the ONE gate for sexually explicit material (Tankoban Discover, Task 1).
//
// Conservative by design: it gates ONLY sexually explicit works. An R / TV-MA / 18 rating, a
// "Mature Readers" imprint, graphic violence, horror, profanity, or dark themes must NEVER trigger
// the gate by themselves — Berserk (R+) and Game of Thrones (TV-MA) stay visible when the setting
// is off. Ecchi is an ordinary visible genre, NOT explicit. Unknown classification defaults to
// VISIBLE: a false positive (hiding mainstream work) is worse than incomplete gating.
//
// Source-aware, not text-aware: trust explicit source flags first, then EXACT normalized
// classifications. Never keyword-match titles or synopses.
//
// A .pragma library can't see context properties, so callers pass the world + normalized item map.
.pragma library

var EXPLICIT_TAGS = {
    "hentai": true,
    "erotica": true,
    "pornography": true,
    "sexually explicit": true,
    "adult film": true
};

// Flatten every classification array on the item into lowercased strings.
// A non-array scalar (e.g. genres:"Hentai") is treated as a single value, not iterated
// character-by-character, so a scalar adapter can't under-gate a real explicit tag.
function values(item) {
    var out = [];
    [item.genres, item.subjects, item.tags, item.categories].forEach(function(xs) {
        var arr = Array.isArray(xs) ? xs : (xs == null ? [] : [xs]);
        for (var i = 0; i < arr.length; i++) out.push(String(arr[i]).toLowerCase());
    });
    return out;
}

// classify(world, item) -> { explicit: bool, reason: string }
function classify(world, item) {
    item = item || {};
    if (item.explicit === true) return { explicit: true, reason: "source-explicit" };
    if (item.behaviorHints && item.behaviorHints.adult === true)
        return { explicit: true, reason: "source-adult" };
    var tags = values(item);
    // hasOwnProperty guard: a genre literally named "constructor" / "__proto__" must NOT
    // match an inherited Object.prototype member and false-positive as explicit.
    for (var i = 0; i < tags.length; i++)
        if (Object.prototype.hasOwnProperty.call(EXPLICIT_TAGS, tags[i]))
            return { explicit: true, reason: "classification:" + tags[i] };
    return { explicit: false, reason: "not-explicit" };
}

// visible(world, item, showExplicit) -> bool. Hidden ONLY when the setting is off and it's explicit.
function visible(world, item, showExplicit) {
    return showExplicit === true || !classify(world, item).explicit;
}

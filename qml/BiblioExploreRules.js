// BiblioExploreRules.js — the pure, keyless brain of the Biblio Explore shelf inventory
// (plan `2026-08-01-biblio-discover-explore.md`, Task 6). Stable shelf key ordering,
// stable extension-key derivation, and customization application (order/hidden). NO QML
// state, NO transport, NO Date.now(): every function is a pure transform so the offscreen
// rules harness can pin it deterministically. The three Explore mosaics (Fiction, Nonfiction,
// Audience) are fixed shelf furniture owned by BiblioExplorePage — they are NEVER part of
// this module's row inventory, so they can never appear in `order`/`hidden` customization.
.pragma library

// ---------------------------------------------------------------------------
// Stable row keys. `top-10` and the four house rails never move relative to each other
// as a GROUP boundary (extensions always sit between Top 10 and the house rails), but
// individual keys within the full list are freely reorderable/hideable via customization.
// ---------------------------------------------------------------------------

var TOP_TEN_KEY = "top-10";
var HOUSE_RAIL_KEYS = ["popular", "top-rated", "new-releases", "trending"];
var EXTENSION_PREFIX = "ext:";

// A stable extension row key derived ONLY from the extension's stable id — never its
// display title, so a title/name edit (or locale change) never orphans a saved preference.
function extensionKey(id) {
    return EXTENSION_PREFIX + String(id);
}

function isExtensionKey(key) {
    return typeof key === "string" && key.indexOf(EXTENSION_PREFIX) === 0;
}

// `extensions` is the list of enabled, input-free book catalogue extensions (the same
// population BiblioDiscoverApi surfaces under "From Your Extensions"): either plain stable
// id strings, or descriptor objects carrying at least `{ id }` (a `title`/`name` is ignored
// for key purposes). Zero enabled extensions collapses the section entirely — no placeholder
// key is emitted, per the empty-extension-section-collapse rule.
function defaultRows(extensions) {
    var list = Array.isArray(extensions) ? extensions : [];
    var keys = [TOP_TEN_KEY];
    var seen = {};
    for (var i = 0; i < list.length; i++) {
        var entry = list[i];
        var id = (entry && typeof entry === "object") ? entry.id : entry;
        if (id === undefined || id === null || id === "") continue;
        var key = extensionKey(id);
        if (seen[key]) continue;          // duplicate stable id -> collapse, don't repeat
        seen[key] = true;
        keys.push(key);
    }
    for (var h = 0; h < HOUSE_RAIL_KEYS.length; h++) keys.push(HOUSE_RAIL_KEYS[h]);
    return keys;
}

// The effective order: saved order first (only entries still present in `rows`, each once),
// then any row in `rows` not yet placed, in `rows`' own (default) order. A saved entry for a
// row that no longer exists (extension removed) is silently dropped, never resurrected.
function _effectiveOrder(rows, order) {
    var available = Array.isArray(rows) ? rows : [];
    var saved = Array.isArray(order) ? order : [];
    var placed = {}, out = [];
    for (var i = 0; i < saved.length; i++) {
        var key = saved[i];
        if (available.indexOf(key) !== -1 && !placed[key]) { placed[key] = true; out.push(key); }
    }
    for (var r = 0; r < available.length; r++) {
        if (!placed[available[r]]) { placed[available[r]] = true; out.push(available[r]); }
    }
    return out;
}

// applyCustomization(rows, {order,hidden}, editMode) — `rows` is the current stable-key
// inventory (normally `defaultRows(extensions)`). Returns a FRESH array of
// `{ key, hidden }` copies in effective order; a hidden row is included (marked hidden:true)
// only in edit mode so the customization UI can still offer it a show/hide toggle, and is
// omitted entirely from normal browsing. A newly available key (e.g. a freshly enabled
// extension) not yet in the saved order is appended safely rather than dropped; a saved key
// for a row that is no longer available is ignored rather than resurrected. Never mutates
// the inputs.
function applyCustomization(rows, customization, editMode) {
    var available = Array.isArray(rows) ? rows.slice() : [];
    var custom = customization || {};
    var order = Array.isArray(custom.order) ? custom.order : [];
    var hidden = Array.isArray(custom.hidden) ? custom.hidden : [];
    var hiddenSet = {};
    for (var h = 0; h < hidden.length; h++) hiddenSet[hidden[h]] = true;

    var effective = _effectiveOrder(available, order);
    var out = [];
    for (var i = 0; i < effective.length; i++) {
        var key = effective[i];
        var isHidden = !!hiddenSet[key];
        if (isHidden && editMode !== true) continue;
        out.push({ key: key, hidden: isHidden });
    }
    return out;
}

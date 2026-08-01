// TheatreRowPreferences — per-tab persistence for the deep catalogue's row customization
// (spec §11). QtCore Settings under [theatreCatalogRows] holds THREE independent JSON strings
// (movies · shows · anime), each parsed defensively into { order:[], hidden:[], renamed:{} }.
// Renaming, hiding, and reordering are keyed by STABLE shelf keys, never titles. Production
// leaves settingsLocation unset (real QSettings store); the harness injects a temp INI url.
import QtQuick
import QtCore

QtObject {
    id: root

    property alias settingsLocation: store.location
    // direct JSON access (also lets the harness seed/inspect a tab); the app uses the API below.
    property alias movies: store.movies
    property alias shows: store.shows
    property alias anime: store.anime
    // emitted ONLY after a real mutation, carrying the tab that changed.
    signal changed(string pageKey)

    property Settings settingsStore: Settings {
        id: store
        category: "theatreCatalogRows"
        property string movies: ""
        property string shows: ""
        property string anime: ""
    }

    function _raw(pageKey) {
        return pageKey === "shows" ? store.shows : pageKey === "anime" ? store.anime : store.movies;
    }
    function _write(pageKey, obj) {
        var s = JSON.stringify(obj);
        if (pageKey === "shows") store.shows = s;
        else if (pageKey === "anime") store.anime = s;
        else store.movies = s;
    }

    // defensive parse — a corrupt or absent value yields the empty default, never throws.
    function valueFor(pageKey) {
        var out = { order: [], hidden: [], renamed: {} };
        var raw = _raw(pageKey);
        if (!raw) return out;
        try {
            var v = JSON.parse(raw);
            if (v && typeof v === "object") {
                out.order = Array.isArray(v.order) ? v.order.slice() : [];
                out.hidden = Array.isArray(v.hidden) ? v.hidden.slice() : [];
                if (v.renamed && typeof v.renamed === "object")
                    for (var k in v.renamed) out.renamed[k] = v.renamed[k];
            }
        } catch (e) { /* corrupt value -> empty default */ }
        return out;
    }

    // the ordered key list the tab actually shows: saved order first (skipping keys no longer
    // available), then any new available keys in their default order.
    function _effectiveOrder(pageKey, availableKeys) {
        var saved = valueFor(pageKey).order;
        var out = [], seen = {};
        for (var i = 0; i < saved.length; i++)
            if (availableKeys.indexOf(saved[i]) !== -1 && !seen[saved[i]]) { seen[saved[i]] = true; out.push(saved[i]); }
        for (var k = 0; k < availableKeys.length; k++)
            if (!seen[availableKeys[k]]) { seen[availableKeys[k]] = true; out.push(availableKeys[k]); }
        return out;
    }

    // move a shelf up (delta -1) or down (+1). Persists the full effective order so the choice
    // survives even before any prior save existed. A boundary move is a silent no-op.
    function move(pageKey, availableKeys, key, delta) {
        var order = _effectiveOrder(pageKey, availableKeys || []);
        var i = order.indexOf(key);
        if (i < 0) return;
        var j = i + delta;
        if (j < 0 || j >= order.length) return;      // boundary -> no-op, no changed()
        order.splice(i, 1);
        order.splice(j, 0, key);
        var v = valueFor(pageKey); v.order = order;
        _write(pageKey, v);
        root.changed(pageKey);
    }

    function toggleHidden(pageKey, key) {
        var v = valueFor(pageKey);
        var idx = v.hidden.indexOf(key);
        if (idx >= 0) v.hidden.splice(idx, 1); else v.hidden.push(key);
        _write(pageKey, v);
        root.changed(pageKey);
    }

    // rename to a non-empty label; an empty/blank label REMOVES the override (reset name).
    function rename(pageKey, key, label) {
        var v = valueFor(pageKey);
        var trimmed = String(label === undefined || label === null ? "" : label).trim();
        if (trimmed.length === 0) delete v.renamed[key];
        else v.renamed[key] = trimmed;
        _write(pageKey, v);
        root.changed(pageKey);
    }

    // page-level reset: restore this tab's default order/visibility/names. Other tabs untouched.
    function reset(pageKey) {
        _write(pageKey, { order: [], hidden: [], renamed: {} });
        root.changed(pageKey);
    }
}

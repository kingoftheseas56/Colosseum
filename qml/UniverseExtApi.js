// UniverseExtApi.js — load, validate and cache a universe extension's payload.
//
// The payload contract is the universes-as-extensions design §5.2. Its end state is a
// served universe.json over HTTPS (§5.5); until that server exists the same document is
// bundled at assets/universes/<file>.json. Same shape, same loader, same validation — so
// the move to HTTPS changes the URL below and nothing else.
//
// VALIDATION IS A GATE, NOT A FORMALITY. A video tile that reaches Theatre without a type
// opens a series as a movie and dies (§5.4). An invalid entry is DROPPED and the rest of
// the payload still renders; a section left empty by that is removed entirely, because an
// empty row is a lie about what the universe holds.
.pragma library

var KINDS = { video: true, manga: true, comic: true, book: true };

// The bundled payload each installed universe extension reads. When these are served,
// this becomes the extension's transportUrl base + "/universe.json".
var FILES = {
    "com.colosseum.universe.onepiece": "one-piece",
    "com.colosseum.universe.dcau":     "dcau"
};
function fileFor(extensionId) { return FILES[extensionId] || ""; }

function _entryOk(kind, e) {
    if (!e || !e.title) return false;
    if (e.manual === true) return true;          // no provider identity, by curation
    if (kind === "video") return !!e.id && (e.type === "movie" || e.type === "series");
    if (kind === "comic") return !!(e.posts && e.posts.length);   // post IDs, never a tag
    return !!e.id;
}

// Returns { title, logo, background, sections: [...] } with everything invalid removed.
function validate(payload) {
    var u = (payload && payload.universe) || {};
    var out = { id: u.id || "", title: u.title || "", logo: u.logo || "",
                background: u.background || "", sections: [] };
    var src = u.sections || [];
    for (var i = 0; i < src.length; i++) {
        var s = src[i];
        if (!s || !s.title || !KINDS[s.kind]) continue;      // unknown kind → skip section
        var kept = [];
        var entries = s.entries || [];
        for (var j = 0; j < entries.length; j++)
            if (_entryOk(s.kind, entries[j])) kept.push(entries[j]);
        if (!kept.length) continue;                          // never render an empty row
        out.sections.push({ id: s.id || "", title: s.title, kind: s.kind, entries: kept });
    }
    return out;
}

var _cache = {};   // extensionId → validated payload

function load(extensionId, done) {
    if (_cache[extensionId]) { done(_cache[extensionId]); return; }
    var file = fileFor(extensionId);
    if (!file) { done(null); return; }
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var parsed = null;
        try { parsed = JSON.parse(xhr.responseText); } catch (e) { parsed = null; }
        if (!parsed) { done(null); return; }
        var v = validate(parsed);
        _cache[extensionId] = v;
        done(v);
    };
    xhr.open("GET", "../assets/universes/" + file + ".json");
    xhr.send();
}

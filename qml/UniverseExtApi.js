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

// Returns { id, title, logo, background, sections: [...] } with everything invalid removed.
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

// extensionId → validated payload. Never invalidated: today's payloads are read-only
// bundled files, so a hit is a hit forever. That stops being true once §5.5's HTTPS
// server can push a revision — this cache becomes a real policy question then, not now.
var _cache = {};

// The payload reader. QML installs the C++ one at startup; tests inject a fake.
var _reader = null;
function setReader(fn) { _reader = fn; }

// Transport is C++'s (ExtensionsStore.universePayload): Qt blocks XMLHttpRequest on
// file:// by default, and house doctrine keeps transport off the GUI thread's JS. The
// callback shape is kept so the HTTPS end state (§5.5) can go async again without
// touching any caller.
function load(extensionId, done) {
    if (_cache[extensionId]) { done(_cache[extensionId]); return; }
    var file = fileFor(extensionId);
    if (!file) { done(null); return; }
    var text = _reader ? _reader(file) : "";
    var parsed = null;
    try { parsed = text ? JSON.parse(text) : null; } catch (e) { parsed = null; }
    if (!parsed) { done(null); return; }        // failures are NOT cached — next call retries
    var v = validate(parsed);
    _cache[extensionId] = v;
    done(v);
}

// NOT wired yet — a later task installs the real reader at startup, in QML, as:
//   UniverseApi.setReader(function (f) { return Extensions.universePayload(f) })
// "Extensions" is the ExtensionsStore singleton's QML context-property name
// (native/main.cpp: engine.rootContext()->setContextProperty("Extensions", extensions)).

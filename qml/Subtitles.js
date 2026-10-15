// Subtitles.js — fetch ONLINE subtitles for a stream, the way Harbor does.
// Endpoint: {base}/subtitles/{type}/{id}.json
//   movie:  type="movie",  id="tt1160419"
//   series: type="series", id="tt0903747:2:7"   (imdb:season:episode)
// The id goes into the path RAW (colons preserved), exactly like Torrentio.
//
// Extensions (spec Phase 4): every ENABLED subtitle extension is queried, in
// installed order — OpenSubtitles v3 stays seeded first (with its resilient host
// trio), and any added well (SubSource, SubDL…) answers alongside it. Results
// merge, dedup by url, and each row keeps its source name so the menu labels the
// group. Main.qml pushes the registry in via setExtensions (a .pragma library
// can't reach context properties). With only the seed installed this behaves
// exactly as the old OpenSubtitles-only fetch.
.pragma library
.import "AddonClient.js" as AddonClient

// OpenSubtitles v3's resilient host trio — the seeded well fans over all three.
var ENDPOINTS = [
    "https://opensubtitles-v3.strem.io",
    "https://opensubtitles.stremio.homes",
    "https://opensubtitles.strem.io"
];
var OPENSUB_SEED_ID = "org.stremio.opensubtitlesv3";

// installed extensions, pushed in from QML (Main.qml owns the wiring)
var extensionsList = [];
function setExtensions(list) {
    extensionsList = list || [];
}

// Preferred language first (Harbor's default preferred = English).
var PREFERRED = ["eng", "en", "english"];

// ISO 639 (2/B 3-letter) → display name, for the common set. Unknown codes fall back
// to the uppercased code so nothing is ever blank.
var LANG_NAMES = {
    eng: "English", en: "English",
    spa: "Spanish", es: "Spanish",
    fre: "French", fra: "French", fr: "French",
    ger: "German", deu: "German", de: "German",
    ita: "Italian", it: "Italian",
    por: "Portuguese", pt: "Portuguese", pob: "Portuguese (BR)",
    rus: "Russian", ru: "Russian",
    ara: "Arabic", ar: "Arabic",
    hin: "Hindi", hi: "Hindi",
    jpn: "Japanese", ja: "Japanese",
    kor: "Korean", ko: "Korean",
    chi: "Chinese", zho: "Chinese", zh: "Chinese",
    dut: "Dutch", nld: "Dutch", nl: "Dutch",
    pol: "Polish", pl: "Polish",
    tur: "Turkish", tr: "Turkish",
    swe: "Swedish", sv: "Swedish",
    dan: "Danish", da: "Danish",
    fin: "Finnish", fi: "Finnish",
    nor: "Norwegian", no: "Norwegian",
    cze: "Czech", ces: "Czech", cs: "Czech",
    gre: "Greek", ell: "Greek", el: "Greek",
    heb: "Hebrew", he: "Hebrew",
    ind: "Indonesian", id: "Indonesian",
    tha: "Thai", th: "Thai",
    vie: "Vietnamese", vi: "Vietnamese",
    ukr: "Ukrainian", uk: "Ukrainian",
    rum: "Romanian", ron: "Romanian", ro: "Romanian",
    hun: "Hungarian", hu: "Hungarian",
    tam: "Tamil", ta: "Tamil",
    tel: "Telugu", te: "Telugu",
    ben: "Bengali", bn: "Bengali",
    mal: "Malayalam", ml: "Malayalam"
};

function langName(code) {
    var c = ("" + (code || "")).toLowerCase().trim();
    if (LANG_NAMES[c]) return LANG_NAMES[c];
    var base = c.split(/[-_]/)[0];
    if (LANG_NAMES[base]) return LANG_NAMES[base];
    return c ? c.toUpperCase() : "Unknown";
}

function _isPreferred(code) {
    var c = ("" + (code || "")).toLowerCase();
    for (var i = 0; i < PREFERRED.length; i++)
        if (c === PREFERRED[i] || c.indexOf(PREFERRED[i]) === 0) return true;
    return false;
}

function _get(url, done) {
    var xhr = new XMLHttpRequest();
    var settled = false;
    function finish(v) { if (!settled) { settled = true; done(v); } }
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { finish(null); return; }
        try { finish(JSON.parse(xhr.responseText)); } catch (e) { finish(null); }
    };
    xhr.open("GET", url);
    xhr.setRequestHeader("Accept", "application/json");
    xhr.send();
}

// Try each base host until one returns a usable subtitle list (the seeded
// OpenSubtitles well fans over its trio; a normal extension has one base).
function _tryBases(idx, bases, type, id, done) {
    if (idx >= bases.length) { done(null); return; }
    var url = bases[idx] + "/subtitles/" + type + "/" + id + ".json";
    _get(url, function(json) {
        if (json && json.subtitles && json.subtitles.length > 0) { done(json); return; }
        _tryBases(idx + 1, bases, type, id, done);
    });
}

// One well's raw response → normalized rows tagged with its source name.
// No cap here — capping happens after the merge so one well can't crowd out another.
function _normalizeWell(raw, sourceName, wellIdx) {
    var subs = (raw && raw.subtitles) ? raw.subtitles : [];
    var out = [];
    var seen = {};
    for (var i = 0; i < subs.length; i++) {
        var s = subs[i];
        if (!s || !s.url) continue;
        if (seen[s.url]) continue;
        seen[s.url] = true;
        var code = s.lang || "";
        out.push({
            id: "ext:" + wellIdx + ":" + (s.id !== undefined ? s.id : i),
            url: s.url,
            lang: code,
            langName: langName(code),
            title: sourceName + " #" + (s.id !== undefined ? s.id : (i + 1)),
            downloads: Number(s.downloads) || 0,
            preferred: _isPreferred(code),
            external: true,
            source: sourceName,
            label: langName(code)
        });
    }
    return out;
}

// Merge all wells: dedup by url (first well keeps it), sort preferred-first then
// by language, cap per (language + source) so every well stays represented.
function _mergeAndCap(lists) {
    var seen = {};
    var all = [];
    for (var w = 0; w < lists.length; w++) {
        var rows = lists[w] || [];
        for (var i = 0; i < rows.length; i++) {
            if (seen[rows[i].url]) continue;
            seen[rows[i].url] = true;
            all.push(rows[i]);
        }
    }
    all.sort(function(a, b) {
        if (a.preferred !== b.preferred) return a.preferred ? -1 : 1;
        if (a.langName !== b.langName) return a.langName < b.langName ? -1 : 1;
        return b.downloads - a.downloads;
    });
    var perKey = {};
    var capped = [];
    for (var j = 0; j < all.length; j++) {
        var k = all[j].lang + "|" + all[j].source;
        perKey[k] = (perKey[k] || 0) + 1;
        if (perKey[k] <= 6) capped.push(all[j]);   // 6 per language per source (Harbor)
    }
    return capped;
}

// The subtitle wells to query for this id, in installed order: enabled extensions
// whose manifest claims the subtitles resource for this type/id. The seeded
// OpenSubtitles well carries its resilient host trio.
function _wells(type, id) {
    var wells = [];
    for (var i = 0; i < extensionsList.length; i++) {
        var e = extensionsList[i];
        if (!e || e.enabled !== true) continue;
        if (!AddonClient.accepts(e.manifest, "subtitles", type, id)) continue;
        // Skip wells that require configuration (account / key) before they answer —
        // querying them blind just 404s. They light up once configured (Harbor's rule).
        var hints = (e.manifest && e.manifest.behaviorHints) || ({});
        if (hints.configurationRequired) continue;
        var name = (e.manifest && e.manifest.name) || e.id;
        var base = String(e.transportUrl).replace(/\/manifest\.json$/i, "");
        wells.push({
            name: name,
            bases: (e.id === OPENSUB_SEED_ID) ? ENDPOINTS.slice() : [base]
        });
    }
    return wells;
}

// Deterministic merge proof (dev harness): two synthetic wells → dedup by url,
// per-(language+source) cap, source labels preserved. No network.
function selfTestMerge() {
    var wellA = _normalizeWell({ subtitles: [
        { id: 1, lang: "eng", url: "http://a/en1.srt" },
        { id: 2, lang: "eng", url: "http://shared/en.srt" },
        { id: 3, lang: "spa", url: "http://a/es1.srt" }
    ] }, "OpenSubtitles v3", 0);
    var wellB = _normalizeWell({ subtitles: [
        { id: 9, lang: "eng", url: "http://shared/en.srt" },   // dup of wellA → dropped
        { id: 8, lang: "fre", url: "http://b/fr1.srt" },
        { id: 7, lang: "eng", url: "http://b/en2.srt" }
    ] }, "SubSource", 1);
    return _mergeAndCap([wellA, wellB]);
}

// Public: fetch(type, id, done) → done([{id,url,lang,langName,label,external,source,preferred}])
function fetch(type, id, done) {
    if (!type || !id) { done([]); return; }
    var wells = _wells(type, id);
    if (!wells.length) {
        // registry not pushed yet (or OpenSubtitles removed) — legacy OpenSubtitles path
        _tryBases(0, ENDPOINTS, type, id, function(json) {
            done(_mergeAndCap([_normalizeWell(json, "OpenSubtitles", 0)]));
        });
        return;
    }
    var results = [];
    var pending = wells.length;
    for (var w = 0; w < wells.length; w++) {
        (function(well, idx) {
            _tryBases(0, well.bases, type, id, function(json) {
                results[idx] = _normalizeWell(json, well.name, idx);
                pending -= 1;
                if (pending === 0) done(_mergeAndCap(results));
            });
        })(wells[w], w);
    }
}

// Pick the best auto-load subtitle (first preferred/English) from a normalized list.
function pickDefault(list) {
    if (!list || !list.length) return null;
    for (var i = 0; i < list.length; i++)
        if (list[i].preferred) return list[i];
    return null;   // no preferred language → don't auto-load (user picks)
}

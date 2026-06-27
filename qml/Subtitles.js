// Subtitles.js — fetch ONLINE subtitles for a stream, the way Harbor does.
// Source: the OpenSubtitles v3 Stremio addon (no login, no key — same ecosystem as
// Cinemeta/Torrentio). Endpoint: {base}/subtitles/{type}/{id}.json
//   movie:  type="movie",  id="tt1160419"
//   series: type="series", id="tt0903747:2:7"   (imdb:season:episode)
// The id goes into the path RAW (colons preserved), exactly like Torrentio.
.pragma library

// Fallback endpoints, tried in order (Harbor uses the same trio).
var ENDPOINTS = [
    "https://opensubtitles-v3.strem.io",
    "https://opensubtitles.stremio.homes",
    "https://opensubtitles.strem.io"
];

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

// Try each endpoint until one returns a usable subtitle list.
function _tryEndpoints(idx, type, id, done) {
    if (idx >= ENDPOINTS.length) { done(null); return; }
    var url = ENDPOINTS[idx] + "/subtitles/" + type + "/" + id + ".json";
    _get(url, function(json) {
        if (json && json.subtitles && json.subtitles.length > 0) { done(json); return; }
        _tryEndpoints(idx + 1, type, id, done);
    });
}

// Normalize + dedupe + sort (preferred language first, then by language name),
// capping each language so one popular title doesn't flood the list.
function _normalize(raw) {
    var subs = (raw && raw.subtitles) ? raw.subtitles : [];
    var seen = {};
    var out = [];
    for (var i = 0; i < subs.length; i++) {
        var s = subs[i];
        if (!s || !s.url) continue;
        var key = (s.lang || "") + "|" + s.url;
        if (seen[key]) continue;
        seen[key] = true;
        var code = s.lang || "";
        var title = "OpenSubtitles V3 #" + (s.id !== undefined ? s.id : (i + 1));
        out.push({
            id: "ext:" + (s.id !== undefined ? s.id : i),
            url: s.url,
            lang: code,
            langName: langName(code),
            title: title,
            downloads: Number(s.downloads) || 0,
            preferred: _isPreferred(code),
            external: true,
            source: "OpenSubtitles",
            label: langName(code)
        });
    }
    out.sort(function(a, b) {
        if (a.preferred !== b.preferred) return a.preferred ? -1 : 1;
        if (a.langName !== b.langName) return a.langName < b.langName ? -1 : 1;
        return 0;
    });
    // per-language cap (6, like Harbor)
    var perLang = {};
    var capped = [];
    for (var j = 0; j < out.length; j++) {
        var l = out[j].lang;
        perLang[l] = (perLang[l] || 0) + 1;
        if (perLang[l] <= 6) capped.push(out[j]);
    }
    return capped;
}

// Public: fetch(type, id, done) → done([{id,url,lang,langName,label,external,source,preferred}])
function fetch(type, id, done) {
    if (!type || !id) { done([]); return; }
    _tryEndpoints(0, type, id, function(json) {
        done(_normalize(json));
    });
}

// Pick the best auto-load subtitle (first preferred/English) from a normalized list.
function pickDefault(list) {
    if (!list || !list.length) return null;
    for (var i = 0; i < list.length; i++)
        if (list[i].preferred) return list[i];
    return null;   // no preferred language → don't auto-load (user picks)
}

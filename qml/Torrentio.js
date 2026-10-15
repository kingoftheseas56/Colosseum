// Torrentio.js - query the Torrentio addon for stream sources and parse each result.
// Single addon, no debrid, no scoring tournament: fetch -> parse -> sort (quality, then seeders).
// type: "movie" | "series"; id: "tt123" (movie) or "tt123:1:2" (series episode).
// NOTE: the id goes into the URL path raw (no encodeURIComponent) because Torrentio needs colons.
.pragma library

var TORRENTIO = "https://torrentio.strem.fun";

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
    xhr.send();
}

function _quality(s) {
    var hay = ((s.name || "") + " " + (s.title || "")).toLowerCase();
    if (/2160p|\b4k\b|uhd/.test(hay)) return "4K";
    if (/1080p/.test(hay)) return "1080p";
    if (/720p/.test(hay)) return "720p";
    if (/480p/.test(hay)) return "480p";
    return "SD";
}

function _rank(q) {
    return q === "4K" ? 4 : q === "1080p" ? 3 : q === "720p" ? 2 : q === "480p" ? 1 : 0;
}

function _seeders(s) {
    var m = (s.title || "").match(/\u{1F464}\s*(\d+)/u);
    return m ? parseInt(m[1], 10) : -1;
}

function _size(s) {
    var m = (s.title || "").match(/\u{1F4BE}\s*([\d.]+\s*[KMGT]B)/u);
    return m ? m[1].replace(/\s+/g, " ").trim() : "";
}

function _release(s) {
    return (s.title || s.name || "").split("\n")[0].trim();
}

function _hay(s) {
    return ((s.name || "") + " " + (s.title || "") + " " +
            ((s.behaviorHints && s.behaviorHints.filename) || "")).toLowerCase();
}

function _contains(arr, v) {
    for (var i = 0; i < arr.length; ++i) {
        if (arr[i] === v) return true;
    }
    return false;
}

function _pushTag(arr, v) {
    if (v && !_contains(arr, v)) arr.push(v);
}

function _tags(s, q) {
    var hay = _hay(s);
    var out = [];
    _pushTag(out, q);
    if (/web[\s.\-]?dl/.test(hay)) _pushTag(out, "WEB-DL");
    else if (/web[\s.\-]?rip/.test(hay)) _pushTag(out, "WEBRip");
    else if (/blu[\s.\-]?ray|bdrip|br[\s.\-]?rip/.test(hay)) _pushTag(out, "BluRay");
    if (/hevc|h[\s.\-]?265|x265/.test(hay)) _pushTag(out, "HEVC");
    else if (/avc|h[\s.\-]?264|x264/.test(hay)) _pushTag(out, "H.264");
    if (/\bdv\b|dolby[\s.\-]?vision/.test(hay)) _pushTag(out, "DV");
    if (/hdr10\+/.test(hay)) _pushTag(out, "HDR10+");
    else if (/\bhdr\b/.test(hay)) _pushTag(out, "HDR");
    if (/atmos/.test(hay)) _pushTag(out, "ATMOS");
    return out;
}

function _qualityLine(tags, q) {
    var left = q;
    if (_contains(tags, "DV")) left += " DV";
    if (_contains(tags, "HDR10+")) return left + " | HDR10+";
    if (_contains(tags, "HDR")) return left + " | HDR";
    return left;
}

function _sourceName(s) {
    var lines = (s.title || "").split("\n");
    for (var i = 1; i < lines.length; ++i) {
        var line = lines[i].replace(/\u{1F464}\s*\d+/gu, "")
                           .replace(/\u{1F4BE}\s*[\d.]+\s*[KMGT]B/gu, "")
                           .replace(/[|•]/g, " ")
                           .replace(/\s+/g, " ")
                           .trim();
        if (line.length > 0 && line.length < 40) return line;
    }
    if (s.name && s.name.length && s.name.length < 40) return s.name;
    return "P2P";
}

// language flags: Torrentio embeds country flags as regional-indicator emoji pairs in the title
// (e.g. the GB + PL flags). Decode each pair back to its 2-letter ISO code.
function _languages(s) {
    var t = String(s.title || "");
    var chars = Array.from(t);
    var out = [];
    for (var i = 0; i < chars.length - 1; ++i) {
        var a = chars[i].codePointAt(0);
        var b = chars[i + 1].codePointAt(0);
        if (a >= 0x1F1E6 && a <= 0x1F1FF && b >= 0x1F1E6 && b <= 0x1F1FF) {
            var code = String.fromCharCode(65 + (a - 0x1F1E6)) + String.fromCharCode(65 + (b - 0x1F1E6));
            if (out.indexOf(code) === -1) out.push(code);
            ++i;
        }
    }
    return out;
}

function parseStream(s) {
    var q = _quality(s);
    var tags = _tags(s, q);
    var langs = _languages(s);
    var multi = /\bmulti\b|multi[\s.\-]?audio|dual[\s.\-]?audio/.test(_hay(s));
    return {
        quality: q,
        rank: _rank(q),
        seeders: _seeders(s),
        size: _size(s),
        release: _release(s),
        tags: tags,
        qualityLine: _qualityLine(tags, q),
        sourceName: _sourceName(s),
        languages: langs,
        audio: (multi || langs.length > 1) ? "Multi Audio" : "English",
        streamKind: "Torrent",
        streamLabel: "P2P stream",
        addonName: "Torrentio",
        infoHash: s.infoHash || "",
        fileIdx: (s.fileIdx !== undefined && s.fileIdx !== null) ? s.fileIdx : 0,
        bingeGroup: (s.behaviorHints && s.behaviorHints.bingeGroup) || "",
        filename: (s.behaviorHints && s.behaviorHints.filename) || ""
    };
}

// done(rows) - rows is [] on any failure/empty, sorted best-first (quality, then seeders).
function loadStreams(type, id, done) {
    if (!type || !id) { done([]); return; }
    var sType = (type === "series") ? "series" : "movie";
    var url = TORRENTIO + "/stream/" + sType + "/" + id + ".json";
    _get(url, function(json) {
        if (!json || !json.streams) { done([]); return; }
        var rows = json.streams.map(parseStream);
        rows.sort(function(a, b) {
            if (b.rank !== a.rank) return b.rank - a.rank;
            return b.seeders - a.seeders;
        });
        done(rows);
    });
}

// AddonClient.js — the generic Stremio-extension caller (spec slice E).
// Where Torrentio.js speaks to ONE addon, this speaks to EVERY installed stream
// extension: Harbor's resource-matching algorithm (src/lib/addons.ts:50-71) +
// parallel fetch with partial results (src/lib/streams/addons.ts), with
// Torrentio.js's proven quality/seeders/language parsing generalized so every
// extension's answers rank the same way. Torrentio.js stays for the
// season-download resolver; the SourcesSheet asks through here.
//
// URL-stream convention: rows with a direct `url` (debrid, HTTP hosts, live tv)
// carry infoHash = "url:<url>" so they flow through the existing play chain
// (playRequested → playTorrent → playStreamAt) with NO signal changes — the
// same routing-prefix trick as the western lane's "gc:" series ids. The player
// branches on the prefix and hands the url straight to mpv.
.pragma library

// per-extension answer window: the slow family gets Harbor's long leash
var FAST_TIMEOUT_MS = 8000;
var SLOW_TIMEOUT_MS = 22000;
var SLOW_RE = /mediafusion|comet|torrentio|knightcrawler|aiostreams|jackettio|torbox/i;

function _get(url, timeoutMs, done) {
    var xhr = new XMLHttpRequest();
    var settled = false;
    function finish(v) { if (!settled) { settled = true; done(v); } }
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { finish(null); return; }
        try { finish(JSON.parse(xhr.responseText)); } catch (e) { finish(null); }
    };
    try { xhr.timeout = timeoutMs; xhr.ontimeout = function() { finish(null); }; } catch (e) { /* older QML XHR */ }
    xhr.open("GET", url);
    xhr.send();
}

// ---------------------------------------------------------------- matching
// Harbor's algorithm: specific resource entries ({name,types,idPrefixes}) win;
// bare string resources fall back to the manifest-level types/idPrefixes.

function _prefixOk(prefixes, id) {
    if (!prefixes || !prefixes.length) return true;
    for (var i = 0; i < prefixes.length; i++)
        if (String(id).indexOf(prefixes[i]) === 0) return true;
    return false;
}

function accepts(manifest, resource, type, id) {
    if (!manifest) return false;
    var resources = manifest.resources || [];
    var specific = [];
    for (var i = 0; i < resources.length; i++)
        if (typeof resources[i] === "object" && resources[i] && resources[i].name === resource)
            specific.push(resources[i]);
    if (specific.length > 0) {
        for (var j = 0; j < specific.length; j++) {
            var r = specific[j];
            var typeOk = r.types && r.types.indexOf(type) !== -1;
            if (typeOk && _prefixOk(r.idPrefixes, id)) return true;
        }
        return false;
    }
    var named = false;
    for (var k = 0; k < resources.length; k++)
        if (resources[k] === resource) { named = true; break; }
    if (!named) return false;
    if (!manifest.types || manifest.types.indexOf(type) === -1) return false;
    return _prefixOk(manifest.idPrefixes, id);
}

// ------------------------------------------------------- parsing (Torrentio.js lineage)

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
    return (s.title || s.name || s.description || "").split("\n")[0].trim();
}

function _hay(s) {
    return ((s.name || "") + " " + (s.title || "") + " " +
            ((s.behaviorHints && s.behaviorHints.filename) || "")).toLowerCase();
}

function _contains(arr, v) {
    for (var i = 0; i < arr.length; ++i)
        if (arr[i] === v) return true;
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

function _sourceName(s, fallback) {
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
    return fallback || "P2P";
}

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

function _host(url) {
    var m = String(url).match(/^[a-z]+:\/\/([^/]+)/i);
    return m ? m[1] : "";
}

// One stream from one extension → a sheet row. Returns null for rows the player
// can't carry (no infoHash AND no direct url — e.g. externalUrl-only addons).
function parseStream(s, addonName, addonPriority) {
    var isTorrent = !!(s.infoHash && String(s.infoHash).length);
    var directUrl = !isTorrent && s.url ? String(s.url) : "";
    if (!isTorrent && !directUrl) return null;

    var q = _quality(s);
    var tags = _tags(s, q);
    var langs = _languages(s);
    var multi = /\bmulti\b|multi[\s.\-]?audio|dual[\s.\-]?audio/.test(_hay(s));
    return {
        quality: q,
        rank: _rank(q),
        seeders: isTorrent ? _seeders(s) : -1,
        size: _size(s),
        release: _release(s),
        tags: tags,
        qualityLine: _qualityLine(tags, q),
        sourceName: isTorrent ? _sourceName(s, "P2P") : (_host(directUrl) || addonName),
        languages: langs,
        audio: (multi || langs.length > 1) ? "Multi Audio" : "English",
        streamKind: isTorrent ? "Torrent" : "Direct",
        streamLabel: isTorrent ? "P2P stream" : "HTTP stream",
        addonName: addonName,
        addonPriority: addonPriority,
        // the routing convention: url rows ride the torrent chain under a prefix
        infoHash: isTorrent ? s.infoHash : ("url:" + directUrl),
        url: directUrl,
        fileIdx: (s.fileIdx !== undefined && s.fileIdx !== null) ? s.fileIdx : 0,
        bingeGroup: (s.behaviorHints && s.behaviorHints.bingeGroup) || "",
        // HTTP hosts that gate on a Referer/Origin deliver the required headers here; the player
        // installs them via mpv.loadFileWithHeaders. Torrent rows carry none. (House HTTP, slice 1.)
        headers: (s.behaviorHints && s.behaviorHints.proxyHeaders) || ({}),
        filename: (s.behaviorHints && s.behaviorHints.filename) || ""
    };
}

// ------------------------------------------------------------ season packs

// Season-pack test (season checkout picker, 2026-07-19). A row answers an
// EPISODE ask, so a pack is a torrent whose NAME doesn't pin that single
// episode but does claim the season — or a complete/batch/episode-range run.
// The filename hint (the file inside the torrent) is ignored on purpose: packs
// carry the episode's file, the torrent name is the truth about the container.
// A false positive degrades gracefully — the resolver falls back to rank-best
// for any episode the picked torrent doesn't actually carry.
function isSeasonPack(row, season) {
    if (row.streamKind !== "Torrent") return false;   // a direct url is one file
    var name = String(row.release || "").toLowerCase();
    if (!name.length) return false;
    // an episode RANGE reads as a run, not one episode: S01E01-E26, (001-1071).
    // Two forms because \b can't sit inside "s01e01-e26" (all word chars): the
    // e-anchored form rides mid-token, the bare-number form needs boundaries.
    var eRange = /e\d{1,4}\s*[-~]\s*e?\d{1,4}\b/.test(name)
              || /\b\d{1,4}\s*[-~]\s*\d{1,4}\b/.test(name);
    // a single-episode pin: S01E05 / 1x05
    var sxe = /s\d{1,2}\s*[._\- ]?e\d{1,4}/.test(name) || /\b\d{1,2}x\d{2,4}\b/.test(name);
    if (sxe && !eRange) return false;
    // anime single-file style: "EP0001", "Ep. 12", " - 1071 " with no season claim
    if (/\bep\.?\s*\d{1,4}\b/.test(name) && !eRange) return false;
    var s = Number(season) || 0;
    if (s > 0) {
        var padded = (s < 10 ? "0" : "") + s;
        if (new RegExp("\\bs" + padded + "\\b").test(name)) return true;       // bare S01
        if (new RegExp("season\\s*" + s + "\\b").test(name)) return true;      // Season 1
    }
    if (/complete|batch|integrale|full\s*season|all\s*episodes/.test(name)) return true;
    return eRange;
}

// ------------------------------------------------------------ aggregation

function _rowKey(r) {
    return r.url ? ("u:" + r.url)
                 : ("t:" + String(r.infoHash).toLowerCase() + ":" + Number(r.fileIdx || 0));
}

function _sortRows(rows) {
    rows.sort(function(a, b) {
        if (a.addonPriority !== b.addonPriority) return a.addonPriority - b.addonPriority;
        if (b.rank !== a.rank) return b.rank - a.rank;
        return b.seeders - a.seeders;
    });
    return rows;
}

// ------------------------------------------------------------ catalogs (spec Phase 3)

function _baseUrl(ext) {
    return String(ext.transportUrl).replace(/\/manifest\.json$/i, "");
}

// True when a catalog can be browsed without extra input (Harbor's rule:
// catalogs whose extra carries isRequired are search/config-only — skip them).
function _browsable(catalog) {
    var extras = (catalog && catalog.extra) || [];
    for (var i = 0; i < extras.length; i++)
        if (extras[i] && extras[i].isRequired) return false;
    return true;
}

// The shelf list a content type gets from the installed extensions, in installed
// order: [{extName, title, url}]. Core rows are skipped — the house's own
// catalogs (Cinemeta) already feed the built-in rows; this is for NEW shelves.
// contentType: "movie" | "series" | "anime".
function catalogSpecs(installedList, contentType) {
    var out = [];
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (!e || e.enabled !== true || e.core === true) continue;
        var m = e.manifest || ({});
        var cats = m.catalogs || [];
        for (var j = 0; j < cats.length; j++) {
            var c = cats[j];
            if (!c || !c.id || !c.type) continue;
            if (c.type !== contentType) continue;
            if (!_browsable(c)) continue;
            out.push({
                extName: m.name || e.id,
                transportUrl: String(e.transportUrl),
                catalogId: String(c.id),
                title: c.name || m.name || "Catalog",
                url: _baseUrl(e) + "/catalog/" + c.type + "/" + encodeURIComponent(c.id) + ".json"
            });
        }
    }
    return out;
}

// One catalog fetch → its meta previews (the standard {metas:[…]} shape).
function fetchCatalog(spec, done) {
    _get(spec.url, FAST_TIMEOUT_MS, function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

// ---------------------------------------------------- service classification (Theatre §8)
// A FIXED table that recognises branded streaming-service catalogues from an extension's
// IDENTITY and its manifest/catalogue NAMES only — never from an arbitrary item title (a
// title like "The Netflix Job" must not brand a random addon). First match wins; no match
// leaves serviceKey "" (the catalogue lands under "From Your Extensions").
var SERVICE_TABLE = [
    { key: "netflix", re: /netflix|\bnfx\b/ },
    { key: "hbo",     re: /hbo\s*max|hbomax|\bhbo\b|\bmax\b/ },
    { key: "appletv", re: /apple\s*tv|appletv|\batv\+?\b/ },
    { key: "disney",  re: /disney|hotstar/ },
    { key: "prime",   re: /prime\s*video|primevideo|amazon\s*prime/ },
    { key: "amc",     re: /\bamc\+?\b/ },
    { key: "fx",      re: /\bfxnow\b|\bfx\b/ }
];

function classifyService(ext, catalog) {
    var m = (ext && ext.manifest) || ({});
    var host = _host(String((ext && ext.transportUrl) || ""));
    var hay = (String((ext && ext.id) || "") + " " + String(m.name || "") + " " + host + " "
               + String((catalog && catalog.name) || "")).toLowerCase();
    for (var i = 0; i < SERVICE_TABLE.length; i++)
        if (SERVICE_TABLE[i].re.test(hay)) return SERVICE_TABLE[i].key;
    return "";
}

// Service-aware catalogue descriptors for the Theatre deep catalogue, in installed order.
// Same enabled/non-core/browsable (no required-extra) filtering as catalogSpecs — transport
// behaviour is unchanged — but each descriptor also carries a classified serviceKey, its
// content type, and a See-all url. The legacy catalogSpecs()/discoverCatalogSpecs() outputs
// are deliberately left untouched for their existing callers.
function theatreCatalogSpecs(installedList, contentType) {
    var out = [];
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (!e || e.enabled !== true || e.core === true) continue;
        var m = e.manifest || ({});
        var cats = m.catalogs || [];
        for (var j = 0; j < cats.length; j++) {
            var c = cats[j];
            if (!c || !c.id || !c.type || c.type !== contentType) continue;
            if (!_browsable(c)) continue;
            out.push({
                serviceKey: classifyService(e, c),
                extName: m.name || e.id,
                transportUrl: String(e.transportUrl),
                type: c.type,
                catalogId: String(c.id),
                title: c.name || m.name || "Catalog",
                url: _baseUrl(e) + "/catalog/" + c.type + "/" + encodeURIComponent(c.id) + ".json"
            });
        }
    }
    return out;
}

// Meta fallback for ids the house sources don't know (spec Phase 3): ask the
// installed extensions that claim this id's meta, first answer wins.
function loadMetaFromExtensions(installedList, type, id, done) {
    var exts = [];
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (!e || e.enabled !== true || e.core === true) continue;
        if (accepts(e.manifest, "meta", type, id)) exts.push(e);
    }
    var sType = (type === "series") ? "series" : "movie";
    function tryNext(index) {
        if (index >= exts.length) { done(null); return; }
        _get(_baseUrl(exts[index]) + "/meta/" + sType + "/" + id + ".json",
             FAST_TIMEOUT_MS, function(json) {
            if (json && json.meta) done(json.meta);
            else tryNext(index + 1);
        });
    }
    tryNext(0);
}

// The ONE place that answers "may we still fall back to Torrentio?".
//
// Torrentio ships seeded but removable (core:false, ExtensionsStore.cpp seed()).
// Several paths reach torrentio.strem.fun by hardcoded URL via Torrentio.js, so
// without this check the store's Remove and off-switch are cosmetic for it: the
// app keeps calling a source the user threw out. Every one of those fallbacks
// asks here first. (2026-07-25, A5 — Torrentio-honesty fix.)
var TORRENTIO_ID = "com.stremio.torrentio.addon";
function torrentioEnabled(installedList) {
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (e && e.id === TORRENTIO_ID)
            return e.enabled === true;
    }
    return false;   // not installed at all
}

// The extensions that would answer a stream ask, in installed (ask) order.
// installedList = Extensions.installed(); type "movie"|"series"; id "tt…"/"tt…:s:e"/"kitsu:…".
function streamExtensions(installedList, type, id) {
    var out = [];
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (!e || e.enabled !== true) continue;
        if (accepts(e.manifest, "stream", type, id)) out.push(e);
    }
    return out;
}

// Ask every extension in parallel; onPartial(rows) fires as each one answers
// (rows = accumulated, deduped, sorted); onDone(rows, askedNames) once all have
// answered or timed out. rows is [] when nothing answers.
function loadStreams(extensions, type, id, onPartial, onDone) {
    var exts = extensions || [];
    if (!type || !id || !exts.length) { onDone([], []); return; }
    var sType = (type === "series") ? "series" : "movie";

    var seen = ({});
    var rows = [];
    var names = [];
    var pending = exts.length;

    function settle(list, ext, priority) {
        var name = (ext.manifest && ext.manifest.name) || ext.id;
        if (list && list.length) {
            for (var i = 0; i < list.length; i++) {
                var row = parseStream(list[i], name, priority);
                if (!row) continue;
                var key = _rowKey(row);
                if (seen[key]) continue;   // first (higher-priority) answer keeps the row
                seen[key] = true;
                rows.push(row);
            }
            _sortRows(rows);
        }
        pending--;
        if (pending > 0) onPartial(rows.slice());
        else onDone(rows.slice(), names.slice());
    }

    for (var i = 0; i < exts.length; i++) {
        (function(ext, priority) {
            var name = (ext.manifest && ext.manifest.name) || ext.id;
            names.push(name);
            var base = String(ext.transportUrl).replace(/\/manifest\.json$/i, "");
            // the id goes into the path raw — Stremio ids need their colons
            var url = base + "/stream/" + sType + "/" + id + ".json";
            var timeoutMs = SLOW_RE.test(ext.transportUrl) ? SLOW_TIMEOUT_MS : FAST_TIMEOUT_MS;
            _get(url, timeoutMs, function(json) {
                settle(json && json.streams ? json.streams : null, ext, priority);
            });
        })(exts[i], i);
    }
}

// ---------------------------------------------------- Discover (arc 2026-07-23)

// Discover's picker wants EVERY installed addon's catalogs INCLUDING core rows
// (Cinemeta's Popular/New are the picker's backbone — unlike the tab shelves,
// which skip core because the house rows already carry Cinemeta).
// A catalog qualifies when every isRequired extra can be auto-answered from its
// options (search-required catalogs stay excluded until the search stage).
function discoverBrowsable(catalog) {
    var extras = (catalog && catalog.extra) || [];
    for (var i = 0; i < extras.length; i++) {
        var x = extras[i];
        if (!x || !x.isRequired) continue;
        if (x.name === "skip") continue;
        var opts = x.options || (x.name === "genre" ? (catalog.genres || []) : []);
        if (x.name === "search" || !opts.length) return false;
    }
    return true;
}

// [{extName, transportUrl, type, catalogId, title, extra, genres, core}] for one
// content type, in installed (ask) order. Used by DiscoverApi only.
function discoverCatalogSpecs(installedList, contentType) {
    var out = [];
    for (var i = 0; i < (installedList || []).length; i++) {
        var e = installedList[i];
        if (!e || e.enabled !== true) continue;
        var m = e.manifest || ({});
        var cats = m.catalogs || [];
        for (var j = 0; j < cats.length; j++) {
            var c = cats[j];
            if (!c || !c.id || !c.type || c.type !== contentType) continue;
            if (!discoverBrowsable(c)) continue;
            out.push({
                extName: m.name || e.id,
                transportUrl: String(e.transportUrl),
                type: c.type,
                catalogId: String(c.id),
                title: c.name || m.name || "Catalog",
                extra: c.extra || [],
                genres: c.genres || [],
                core: e.core === true
            });
        }
    }
    return out;
}

// /catalog/{type}/{id}[/{extraProps}].json — extraPairs = [["genre","Action"]],
// skip appended when > 0. Values are URI-encoded; the path id too.
function catalogUrl(transportUrl, type, catalogId, extraPairs, skip) {
    var base = String(transportUrl).replace(/\/manifest\.json$/i, "");
    var url = base + "/catalog/" + type + "/" + encodeURIComponent(catalogId);
    var parts = [];
    for (var i = 0; i < (extraPairs || []).length; i++) {
        var p = extraPairs[i];
        if (p && p[0] && p[1] !== null && p[1] !== undefined && String(p[1]).length)
            parts.push(p[0] + "=" + encodeURIComponent(p[1]));
    }
    if (skip && skip > 0) parts.push("skip=" + skip);
    if (parts.length) url += "/" + parts.join("&");
    return url + ".json";
}

// One page of one catalog → metas array (defensive cap 100).
function fetchCatalogUrl(url, done) {
    _get(url, FAST_TIMEOUT_MS, function(json) {
        var metas = (json && json.metas) ? json.metas : [];
        done(metas.slice(0, 100));
    });
}

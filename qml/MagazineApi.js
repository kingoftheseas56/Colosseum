// MagazineApi — the magazine template's registry lane (A5, 2026-07-12, Hemanth: "use the
// MAL magazine page to create something more custom and unique"). MAL is the ONLY database
// with a serialization axis; Jikan is its keyless door (no-login law). The registry call
// (/manga?magazines=<id>) is MAL's own serialization LIST, not a fuzzy search — exact by
// magazine id, so live data IS the canon here (no impostor risk, no curation needed).
// Fetches are serial (Jikan allows ~3/sec), cached per session, and HONEST under failure:
// whatever pages landed are served, nothing is invented, empty means the page's curated
// fallback carries the room.
.pragma library

var JIKAN = "https://api.jikan.moe/v4";

// The eras of Jump — spans only (no taste): membership is decided by each entry's REAL
// serialization start year from the registry.
var ERAS = [
    { era: "The Foundation",     from: 1968, to: 1979, span: "1968–1979" },
    { era: "The Golden Age",     from: 1980, to: 1999, span: "1980–1999" },
    { era: "The Big Three Era",  from: 2000, to: 2009, span: "2000–2009" },
    { era: "The New Generation", from: 2010, to: 2019, span: "2010–2019" },
    { era: "This Decade",        from: 2020, to: 9999, span: "2020–" }
];

var _cache = {};   // magazineId -> { total, all: [entry], publishing: [entry] }

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

// one Jikan manga record → the page's entry shape (English title preferred — the manga
// lane's search speaks English; MAL default titles are romaji)
function mapEntry(m) {
    var prop = (m && m.published && m.published.prop) ? m.published.prop : {};
    return {
        malId: m.mal_id || 0,
        title: m.title_english || m.title || "",
        cover: (m.images && m.images.jpg && (m.images.jpg.large_image_url || m.images.jpg.image_url)) || "",
        fromYear: (prop.from && prop.from.year) || 0,
        toYear: (prop.to && prop.to.year) || 0,
        publishing: (m.status || "") === "Publishing",
        members: m.members || 0,
        score: m.score || 0,
        chapters: m.chapters || 0
    };
}

// registry → era shelves, each shelf members-ranked; empty eras vanish (never a bare shelf)
function bucketByEra(list) {
    return ERAS.map(function(e) {
        return { era: e.era, span: e.span,
                 items: list.filter(function(m) { return m.fromYear >= e.from && m.fromYear <= e.to; })
                            .sort(function(a, b) { return b.members - a.members; }) };
    }).filter(function(b) { return b.items.length > 0; });
}

// readable circulation: 2143567 → "2.1M", 96432 → "96k" (live numbers only — never faked)
function fmtMembers(n) {
    if (!n || n <= 0) return "";
    if (n >= 1000000) return (Math.round(n / 100000) / 10) + "M";
    if (n >= 1000) return Math.round(n / 1000) + "k";
    return String(n);
}

// loadRegistry(magazineId, done) — the whole lane in one call. Serially walks the
// members-ranked registry (4 pages = top 100) then the currently-publishing slice
// (2 pages), and emits ONCE: { total, all, publishing }. done(null) only when NOTHING
// landed (feed down) — partial pages still serve.
function loadRegistry(magazineId, done) {
    if (_cache[magazineId]) { done(_cache[magazineId]); return; }

    var base = JIKAN + "/manga?magazines=" + magazineId + "&order_by=members&sort=desc&limit=25";
    var all = [], publishing = [], total = 0;

    function finish() {
        if (!all.length && !publishing.length) { done(null); return; }
        var seen = {};
        all.forEach(function(m) { seen[m.malId] = true; });
        var reg = { total: total, all: all, publishing: publishing };
        _cache[magazineId] = reg;
        done(reg);
    }

    function walkPublishing(pageNo) {
        if (pageNo > 2) { finish(); return; }
        requestJson(base + "&status=publishing&page=" + pageNo, function(j) {
            if (!j || !j.data) { finish(); return; }
            publishing = publishing.concat(j.data.map(mapEntry));
            if (j.pagination && j.pagination.has_next_page) walkPublishing(pageNo + 1);
            else finish();
        });
    }

    function walkAll(pageNo) {
        if (pageNo > 4) { walkPublishing(1); return; }
        requestJson(base + "&page=" + pageNo, function(j) {
            if (!j || !j.data) { walkPublishing(1); return; }
            all = all.concat(j.data.map(mapEntry));
            if (j.pagination && j.pagination.items && j.pagination.items.total)
                total = j.pagination.items.total;
            if (j.pagination && j.pagination.has_next_page) walkAll(pageNo + 1);
            else walkPublishing(1);
        });
    }

    walkAll(1);
}

// MagazineApi — the magazine registry lane, third form: THE LONG RUN (A5, 2026-07-18,
// fresh design on Hemanth's order — the archive/era machinery is gone). MAL is the ONLY
// database with a serialization axis; Jikan is its keyless door (no-login law). The
// registry call (/manga?magazines=<id>) is MAL's own serialization LIST, not a fuzzy
// search — exact by magazine id, so live data IS the canon. AniList cannot replace this
// lane (it has no magazine axis at all) — it carries the ART instead: the curated
// flagships in Universes.js ride verified AniList id-pins with baked covers.
//
// Two lanes:
//   FAST lane    — loadSummary: top 100 by MAL members + the current publishing registry.
//                  Feeds the hero total, the run chart's live strokes, Running Now, and
//                  Most Collected.
//   ARCHIVE lane — fetchArchivePage: ONE registry page per call. The PAGE drives the walk
//                  with its own Timer (Jikan allows ~3/sec — pacing is a UI concern, the
//                  library stays pure fetch+map). Per-page session cache makes a re-walk
//                  instant; a failed walk RESUMES from the failed page (mergeDedup is
//                  id-keyed, so resume can never duplicate).
//
// HONESTY LAW: `members` is a MyAnimeList member/library count — no caller may label it
// anything print-shaped. Nothing here invents totals, dates, or entries: a failed fetch
// returns null and whatever already landed stands.
.pragma library

var JIKAN = "https://api.jikan.moe/v4";

var _summaryCache = {};   // magazineId -> { total, all, publishing }
var _pageCache = {};      // "magazineId:page" -> { entries, hasNext, lastPage, total }

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

// "Oda, Eiichiro" (MAL's file-by-surname form) → "Eiichiro Oda" (a printed byline)
function byline(name) {
    var parts = String(name || "").split(", ");
    return parts.length === 2 ? parts[1] + " " + parts[0] : String(name || "");
}

// one Jikan manga record → the page's entry shape (English title preferred — the manga
// lane's search speaks English; MAL default titles are romaji)
function mapEntry(m) {
    var prop = (m && m.published && m.published.prop) ? m.published.prop : {};
    return {
        malId: m.mal_id || 0,
        title: m.title_english || m.title || "",
        author: (m.authors && m.authors.length) ? byline(m.authors[0].name) : "",
        cover: (m.images && m.images.jpg && (m.images.jpg.large_image_url || m.images.jpg.image_url)) || "",
        fromYear: (prop.from && prop.from.year) || 0,
        toYear: (prop.to && prop.to.year) || 0,
        publishing: (m.status || "") === "Publishing",
        members: m.members || 0,
        score: m.score || 0,
        chapters: m.chapters || 0
    };
}

// a curated flagship (Universes.js shape) → the same entry shape the live registry uses,
// so the run chart and the offline roster ride one code path
function mapFlagship(f) {
    return {
        malId: 0, anilistId: f.al || 0,
        title: f.t || "", author: f.a || "", cover: f.cover || "",
        fromYear: f.from || 0, toYear: f.to || 0,
        publishing: !!f.publishing, members: 0, score: 0, chapters: 0
    };
}

// ── THE RUN CHART ─────────────────────────────────────────────────────────────────────
// buildRuns(list, maxN, nowYear) — the serialization strokes, packed into lanes. Takes
// the top maxN by MAL members (dated entries only), orders them by real start year, and
// greedily assigns each to the first lane whose previous run ended 2+ years earlier (the
// gap keeps stroke labels from colliding). A publishing run's right edge is nowYear.
// Pure and order-deterministic — headless-tested.
function buildRuns(list, maxN, nowYear) {
    var cands = list.filter(function(m) { return m.fromYear > 0; })
                    .sort(function(a, b) { return b.members - a.members; })
                    .slice(0, maxN)
                    .sort(function(a, b) { return (a.fromYear - b.fromYear)
                                               || String(a.title).localeCompare(String(b.title)); });
    var laneEnds = [];
    var runs = cands.map(function(m) {
        var end = m.publishing ? nowYear : (m.toYear || m.fromYear);
        var lane = -1;
        for (var i = 0; i < laneEnds.length; i++)
            if (laneEnds[i] + 2 <= m.fromYear) { lane = i; break; }
        if (lane < 0) { lane = laneEnds.length; laneEnds.push(0); }
        laneEnds[lane] = end;
        return { title: m.title, author: m.author || "", fromYear: m.fromYear,
                 toYear: m.toYear || 0, endFor: end, publishing: !!m.publishing,
                 members: m.members || 0, cover: m.cover || "", lane: lane };
    });
    return { runs: runs, lanes: laneEnds.length };
}

// the registry wall's sort switch
function sortBy(items, mode) {
    var out = items.slice();
    if (mode === "alpha")
        out.sort(function(a, b) { return String(a.title).localeCompare(String(b.title)); });
    else if (mode === "year")
        out.sort(function(a, b) { return (b.fromYear - a.fromYear)
                                      || String(a.title).localeCompare(String(b.title)); });
    else
        out.sort(function(a, b) { return b.members - a.members; });
    return out;
}

// "1984" → "1980s" (the registry wall's decade filter; 0 = undated)
function decadeOf(year) {
    if (!year || year <= 0) return "";
    return (Math.floor(year / 10) * 10) + "s";
}

// merge an archive batch into the accumulated registry, id-keyed — resuming a failed
// walk can never duplicate an entry. Returns a NEW array so QML bindings refresh.
function mergeDedup(accumulated, incoming) {
    var seen = {};
    accumulated.forEach(function(m) { seen[m.malId] = true; });
    var out = accumulated.slice();
    (incoming || []).forEach(function(m) {
        if (!m.malId || seen[m.malId]) return;
        seen[m.malId] = true;
        out.push(m);
    });
    return out;
}

// readable member counts: 2143567 → "2.1M", 96432 → "96k" (live numbers only — never faked)
function fmtMembers(n) {
    if (!n || n <= 0) return "";
    if (n >= 1000000) return (Math.round(n / 100000) / 10) + "M";
    if (n >= 1000) return Math.round(n / 1000) + "k";
    return String(n);
}

// ── FAST LANE ─────────────────────────────────────────────────────────────────────────
// loadSummary(magazineId, done) — top 100 by MAL members (4 pages) + the currently-
// publishing registry (2 pages), emitted ONCE: { total, all, publishing }. done(null)
// only when NOTHING landed (feed down) — partial pages still serve.
function loadSummary(magazineId, done) {
    if (_summaryCache[magazineId]) { done(_summaryCache[magazineId]); return; }

    var base = JIKAN + "/manga?magazines=" + magazineId + "&order_by=members&sort=desc&limit=25";
    var all = [], publishing = [], total = 0;

    function finish() {
        if (!all.length && !publishing.length) { done(null); return; }
        var summary = { total: total, all: all, publishing: publishing };
        _summaryCache[magazineId] = summary;
        done(summary);
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

// ── ARCHIVE LANE ──────────────────────────────────────────────────────────────────────
// hasPage — true when a registry page already sits in the session cache (the page walker
// skips its throttle for cached pages, so a revisit files instantly)
function hasPage(magazineId, pageNo) {
    return !!_pageCache[magazineId + ":" + pageNo];
}

// fetchArchivePage(magazineId, pageNo, done) — one registry page, mapped and cached.
// done(null) on failure (the caller resumes from this same page later); otherwise
// { entries, hasNext, lastPage, total }.
function fetchArchivePage(magazineId, pageNo, done) {
    var key = magazineId + ":" + pageNo;
    if (_pageCache[key]) { done(_pageCache[key]); return; }
    var url = JIKAN + "/manga?magazines=" + magazineId
            + "&order_by=members&sort=desc&limit=25&page=" + pageNo;
    requestJson(url, function(j) {
        if (!j || !j.data) { done(null); return; }
        var res = {
            entries: j.data.map(mapEntry),
            hasNext: !!(j.pagination && j.pagination.has_next_page),
            lastPage: (j.pagination && j.pagination.last_visible_page) || 0,
            total: (j.pagination && j.pagination.items && j.pagination.items.total) || 0
        };
        _pageCache[key] = res;
        done(res);
    });
}

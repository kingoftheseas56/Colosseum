// LocgApi.js — League of Comic Geeks (leagueofcomicgeeks.com/comic/get_comics): the
// AniList-model metadata catalogue brain for the comics lane. This file is PARSERS ONLY
// (Task 2) — fetch verbs (spaced queue, caches, validation) land in Task 3.
// Contract captured live 2026-07-09 — tests/fixtures/locg/FINDINGS.md is the markup
// contract; the 4 fixtures in tests/fixtures/locg/ are the real JSON this parses.
//
// TWO distinct <li> item shapes (parser must branch on shape, never assume one):
//   1. series/search shape (search.json, popular.json): plain <li>, no attributes.
//      Series id ONLY in the href (/comics/series/<id>/<slug>), publisher is the FIRST
//      bare <span class=""> in the copy-really-small div, second such span holds the
//      year (" &nbsp;·&nbsp; <year>"). No genre anywhere.
//   2. releases/issue shape (releases.json, and series.json's issue list): <li
//      class="issue" data-comic="<id>" data-pulls="<n>" data-community="<n>" ...> — id/
//      pulls/rating are plain attributes (no text scraping). Publisher is a literal
//      <div class="publisher color-offset">. Cover data-src carries a cache-busting
//      query string. Date rides <span class="date" data-date="<unix>">Human</span>.
.pragma library

function decodeEntities(s) {
    return String(s).replace(/&amp;/g, "&").replace(/&#0?39;/g, "'").replace(/&quot;/g, '"')
                    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&nbsp;/g, " ")
                    .replace(/&#183;/g, "·");
}

// Series/search shape: search.json + popular.json (list=search&list_option=series).
// Returns [{ id:"locg:<id>", title, cover, publisher, startYear }]. Never throws —
// non-matching input (garbage HTML, wrong shape) yields [].
function parseSeriesList(fragment) {
    var out = [];
    if (!fragment || typeof fragment !== "string") return out;
    try {
        var liRe = /<li>([\s\S]*?)<\/li>/g;
        var m;
        while ((m = liRe.exec(fragment)) !== null) {
            var block = m[1];
            var idm = block.match(/\/comics\/series\/(\d+)\//) || block.match(/data-id="(\d+)"/);
            if (!idm) continue;
            var id = idm[1];

            var titleM = block.match(/<div class="title color-primary">[\s\S]*?<a[^>]*>([\s\S]*?)<\/a>/);
            var title = titleM ? decodeEntities(titleM[1].trim()) : "";

            var coverM = block.match(/data-src="([^"]+)"/);
            var cover = coverM ? coverM[1] : "";

            var publisher = "";
            var startYear = 0;
            var copyM = block.match(/<div class="copy-really-small[^"]*">([\s\S]*?)<\/div>/);
            if (copyM) {
                var spans = [];
                var spanRe = /<span class="">([\s\S]*?)<\/span>/g;
                var sm;
                while ((sm = spanRe.exec(copyM[1])) !== null) spans.push(decodeEntities(sm[1].trim()));
                if (spans.length > 0) publisher = spans[0];
                if (spans.length > 1) {
                    var yearM = spans[1].match(/(\d{4})/);
                    if (yearM) startYear = parseInt(yearM[1], 10);
                }
            }

            out.push({
                id: "locg:" + id,
                title: title,
                cover: cover,
                publisher: publisher,
                startYear: startYear
            });
        }
    } catch (e) { return []; }
    return out;
}

// Releases/issue shape: releases.json (and reused for series.json's per-issue list).
// Returns [{ id:"locg:<id>", title, cover, publisher, date, pulls (number), rating (number) }].
// Never throws — non-matching input yields [].
function parseReleases(fragment) {
    var out = [];
    if (!fragment || typeof fragment !== "string") return out;
    try {
        var liRe = /<li class="issue[^"]*"([^>]*)>([\s\S]*?)<\/li>/g;
        var m;
        while ((m = liRe.exec(fragment)) !== null) {
            var attrs = m[1];
            var block = m[2];

            var idm = attrs.match(/data-comic="(\d+)"/);
            if (!idm) continue;
            var id = idm[1];

            var pullsM = attrs.match(/data-pulls="(\d+)"/);
            var pulls = pullsM ? parseInt(pullsM[1], 10) : 0;

            var ratingM = attrs.match(/data-community="(\d+)"/);
            var rating = ratingM ? parseInt(ratingM[1], 10) : 0;

            var titleM = block.match(/<div class="title color-primary"[^>]*>[\s\S]*?<a[^>]*>([\s\S]*?)<\/a>/);
            var title = titleM ? decodeEntities(titleM[1].trim()) : "";

            var pubM = block.match(/<div class="publisher color-offset">([\s\S]*?)<\/div>/);
            var publisher = pubM ? decodeEntities(pubM[1].trim()) : "";

            var coverM = block.match(/data-src="([^"]+)"/);
            var cover = coverM ? coverM[1] : "";

            var dateM = block.match(/<span class="date" data-date="(\d+)">([\s\S]*?)<\/span>/);
            var date = dateM ? decodeEntities(dateM[2].trim()) : "";
            var dateUnix = dateM ? parseInt(dateM[1], 10) : 0;

            out.push({
                id: "locg:" + id,
                title: title,
                cover: cover,
                publisher: publisher,
                date: date,
                dateUnix: dateUnix,
                pulls: pulls,
                rating: rating
            });
        }
    } catch (e) { return []; }
    return out;
}

// Series detail: series.json returns a STRUCTURED object — {"series":{"title",
// "publisher_name",...}, list:"<issues HTML>"} — richer than a bare fragment. Also
// tolerates being handed a bare fragment/garbage string (never throws).
// Returns { issues, issueCount, startYear, publisher, title }.
function parseSeriesDetail(json_or_fragment) {
    var empty = { issues: [], issueCount: 0, startYear: 0, publisher: "", title: "" };
    if (!json_or_fragment) return empty;
    try {
        var obj = null;
        if (typeof json_or_fragment === "string") {
            try { obj = JSON.parse(json_or_fragment); } catch (e) { obj = null; }
        } else if (typeof json_or_fragment === "object") {
            obj = json_or_fragment;
        }

        var listFragment = "";
        var seriesObj = null;
        if (obj && typeof obj === "object") {
            if (typeof obj.list === "string") listFragment = obj.list;
            if (obj.series && typeof obj.series === "object") seriesObj = obj.series;
        } else if (typeof json_or_fragment === "string") {
            // not JSON — treat the raw string as the fragment itself (best-effort)
            listFragment = json_or_fragment;
        }

        var issues = parseReleases(listFragment);

        var startYear = 0;
        for (var i = 0; i < issues.length; i++) {
            if (issues[i].dateUnix > 0) {
                var y = new Date(issues[i].dateUnix * 1000).getFullYear();
                if (startYear === 0 || y < startYear) startYear = y;
            }
        }

        var title = "";
        var publisher = "";
        if (seriesObj) {
            if (typeof seriesObj.title === "string") title = decodeEntities(seriesObj.title.trim());
            if (typeof seriesObj.publisher_name === "string") publisher = decodeEntities(seriesObj.publisher_name.trim());
            if (!startYear && typeof seriesObj.start_year !== "undefined") {
                var sy = parseInt(seriesObj.start_year, 10);
                if (!isNaN(sy)) startYear = sy;
            }
        }
        if (!title && issues.length > 0) title = issues[0].title;
        if (!publisher && issues.length > 0) publisher = issues[0].publisher;

        return {
            issues: issues,
            issueCount: issues.length,
            startYear: startYear,
            publisher: publisher,
            title: title
        };
    } catch (e) {
        return empty;
    }
}

// ── polite spaced queue. fetchFn/delayFn INJECTED (Main.qml sets real XHR + a Timer spacer;
//    tests set fakes) so the module stays pure/testable — the XoxoApi nowFn lesson. ──
var UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
var SPACING_MS = 500;
var fetchFn = null;                          // function(url, cb(bodyOrNull))
var delayFn = function(ms, cb) { cb(); };    // Main.qml overrides with a Timer-backed spacer
var nowFn = function() { return 0; };        // Main.qml injects Date.now
var _q = [];
var _busy = false;
var _testLog = [];   // test-only fetch-url log (tests push via fetchFn injection; harmless in prod)
function _defaultFetch(url, cb) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        cb((xhr.status >= 200 && xhr.status < 300) ? xhr.responseText : null);
    };
    xhr.open("GET", url);
    xhr.setRequestHeader("User-Agent", UA);
    xhr.setRequestHeader("X-Requested-With", "XMLHttpRequest");
    xhr.send();
}
function _pump() {
    if (_busy || _q.length === 0) return;
    _busy = true;
    var job = _q.shift();
    (fetchFn || _defaultFetch)(job.url, function(body) {
        delayFn(SPACING_MS, function() { _busy = false; _pump(); });
        job.done(body);
    });
}
function _enqueue(url, done) { _q.push({ url: url, done: done }); _pump(); }

// ── validation: usable = JSON with a numeric count and a string list. Else blocked meta,
//    never parsed (a CF challenge page or outage HTML can never masquerade as data). ──
function _meta(ok) { return { ok: ok, blocked: !ok }; }
function _validated(body) {
    if (!body) return null;
    var r; try { r = JSON.parse(body); } catch (e) { return null; }
    var count = (typeof r.series_count === "number") ? r.series_count : r.count;
    if (typeof count !== "number" || typeof r.list !== "string") return null;
    return r;
}

// ── URL builders (exposed so tests pin the exact contract) ──
var BASE_URL = "https://leagueofcomicgeeks.com/comic/get_comics";
function searchUrl(q) {
    return BASE_URL + "?list=search&list_option=series&view=thumbs&title=" +
           encodeURIComponent(String(q).trim()) + "&order=alpha-asc&format%5B%5D=1&format%5B%5D=6";
}
function releasesUrl() {
    var d = new Date(nowFn());
    return BASE_URL + "?list=releases&view=thumbs&format%5B%5D=1%2C6&date_type=week&date=" +
           (d.getMonth() + 1) + "/" + d.getDate() + "/" + d.getFullYear() + "&order=pulls";
}
function popularUrl() {
    return BASE_URL + "?list=search&list_option=series&view=thumbs&title=&order=pulls&format%5B%5D=1&format%5B%5D=6";
}
function seriesUrl(locgId) {
    return BASE_URL + "?list=search&view=thumbs&format%5B%5D=1&series_id=" +
           String(locgId).replace(/^locg:/, "") + "&character=0&order=date-desc";
}

// ── session cache: parsed results by URL. A catalogue page is fetched ONCE per session. ──
var _cache = {};
function _cachedVerb(url, parse, done) {
    if (_cache[url]) { done(_cache[url], _meta(true)); return; }
    _enqueue(url, function(body) {
        var r = _validated(body);
        if (!r) { done([], _meta(false)); return; }
        var parsed = parse(r.list);
        _cache[url] = parsed;
        done(parsed, _meta(true));
    });
}

// ── the catalogue verbs. done(result, meta) — meta = {ok, blocked}. ──
function searchSeries(query, done) { _cachedVerb(searchUrl(query), parseSeriesList, done); }
function popular(done)            { _cachedVerb(popularUrl(), parseSeriesList, done); }
function releases(done)           { _cachedVerb(releasesUrl(), parseReleases, done); }
function top10ThisWeek(done) {
    releases(function(list, meta) {
        done(list.slice().sort(function(a, b) { return b.pulls - a.pulls; }).slice(0, 10), meta);
    });
}
function series(locgId, done) {
    var url = seriesUrl(locgId);
    if (_cache[url]) { done(_cache[url], _meta(true)); return; }
    _enqueue(url, function(body) {
        var r = _validated(body);
        if (!r) { done(null, _meta(false)); return; }
        var det = parseSeriesDetail(body);   // parseSeriesDetail takes the whole JSON (structured series obj)
        _cache[url] = det;
        done(det, _meta(true));
    });
}

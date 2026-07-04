// ComicsApi.js — the western-comics catalog: GetComics IS both catalog and download
// (ratified 2026-07-04, RCO retired). All catalog reads are the WP REST API — clean
// JSON, no HTML scraping:
//   • series search  → /wp-json/wp/v2/tags?search=…      (a tag IS a series)
//   • series shelf   → /wp-json/wp/v2/posts?tags=<id>    (a release post IS the volume)
// Covers: each release's own og_image (exact match by construction). iTunes ebook
// search supplies the SERIES-level poster only — the one place fuzzy matching is
// safe, because a wrong poster can't download a wrong edition.
// The actual download happens in C++ (`Comics` / ComicDownloader): signed
// DOWNLOAD-NOW link → comicfiles.ru archive → extracted page dir.
.pragma library

var GC = "https://getcomics.org/wp-json/wp/v2";
var ITUNES = "https://itunes.apple.com/search";

// A browser UA on every call — the engine NAM stamps "Colosseum/0.1" otherwise,
// which Cloudflare-fronted hosts throttle much sooner (the Fandom-UA lesson).
var UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";

// ── GetComics request queue ──
// The WP API 429s bursts HARD (harness-proven 2026-07-04: even 2-concurrent trips
// it, and an immediate retry just re-bounces off the limiter). So: ONE request in
// flight, and a 429'd job goes to the BACK of the queue — the jobs ahead of it are
// its cool-down (library JS has no timers). Two requeues, then it fails for real.
// done(json, totalPages, totalItems).
var _gcQueue = [], _gcActive = 0, GC_CONCURRENCY = 1;

function gcJson(url, done) {
    _gcQueue.push({ u: url, d: done, tries: 0 });
    _gcPump();
}
function _gcPump() {
    while (_gcActive < GC_CONCURRENCY && _gcQueue.length) {
        _gcActive += 1;
        _gcFire(_gcQueue.shift());
    }
}
function _gcFire(job) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var ok = xhr.status >= 200 && xhr.status < 300;
        if (!ok && job.tries < 2) {
            job.tries += 1;
            _gcActive -= 1;
            _gcQueue.push(job);          // retry LATER, behind whatever is pending
            _gcPump();
            return;
        }
        if (!ok) console.warn("[ComicsApi] GC request failed status=" + xhr.status
                              + " url=…" + job.u.slice(-70));
        _gcActive -= 1;
        var out = null, tp = 0, tot = 0;
        if (ok) {
            tp  = Number(xhr.getResponseHeader("X-WP-TotalPages") || 0);
            tot = Number(xhr.getResponseHeader("X-WP-Total") || 0);
            try { out = JSON.parse(xhr.responseText); } catch (e) { out = null; }
        }
        try { job.d(out, tp, tot); } finally { _gcPump(); }
    };
    xhr.open("GET", job.u);
    xhr.setRequestHeader("User-Agent", UA);
    xhr.send();
}

// plain fetch for non-GetComics hosts (iTunes) — their limits are separate
function reqJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.setRequestHeader("User-Agent", UA);
    xhr.send();
}

// WP titles come HTML-entity-encoded ("Avatar &#8211; The Last Airbender &#8230;")
function decodeEntities(s) {
    return String(s || "")
        .replace(/&#8211;|&ndash;/g, "–").replace(/&#8212;|&mdash;/g, "—")
        .replace(/&#8216;/g, "‘").replace(/&#8217;/g, "’")
        .replace(/&#8220;/g, "“").replace(/&#8221;/g, "”")
        .replace(/&#038;|&amp;/g, "&").replace(/&#8230;|&hellip;/g, "…")
        .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, "\"")
        .replace(/&#(\d+);/g, function(m, n) { return String.fromCharCode(Number(n)); });
}

// ── series search: GetComics tags. A tag = a series page (newest-first releases). ──
// WP's tag search is token-OR and alphabetical ("avatar the last airbender" leads
// with "A Taste for Blood"), so: pull by release count, then rank by how many query
// tokens the tag name actually contains — the real series floats to the top.
var seriesCache = {};   // normalized query → ranked tag list (successes only — quota is precious)

function searchSeries(query, done) {
    var key = query.trim().toLowerCase();
    if (seriesCache[key]) { done(seriesCache[key]); return; }
    var q = encodeURIComponent(query.trim());
    gcJson(GC + "/tags?search=" + q + "&per_page=20&orderby=count&order=desc"
            + "&_fields=id,name,slug,count", function(j) {
        if (!j || !j.length) { done([]); return; }
        var ql = query.trim().toLowerCase();
        var toks = ql.split(/\s+/).filter(function(t) { return t.length > 1 });
        var out = [];
        for (var i = 0; i < j.length; i++) {
            if (!j[i].count) continue;                       // empty tag = no releases
            var name = decodeEntities(j[i].name);
            var nl = name.toLowerCase(), hits = 0;
            for (var t = 0; t < toks.length; t++)
                if (nl.indexOf(toks[t]) >= 0) hits += 1;
            // the exact-named series must beat a bigger fuzzy cousin ("Invincible"
            // vs "Invincible Iron Man", "Batman" vs "Batman Beyond"): exact name
            // outranks everything, prefix outranks plain token hits.
            if (nl === ql) hits += 100;
            else if (nl.indexOf(ql) === 0) hits += 10;
            out.push({ tagId: j[i].id, tag: j[i].slug, title: name,
                       count: j[i].count, _hits: hits });
        }
        out.sort(function(a, b) { return (b._hits - a._hits) || (b.count - a.count); });
        var ranked = out.slice(0, 6);
        seriesCache[key] = ranked;
        done(ranked);
    });
}

function tagBySlug(slug, done) {
    gcJson(GC + "/tags?slug=" + encodeURIComponent(slug) + "&_fields=id,name,slug,count",
        function(j) {
            if (!j || !j.length) { done(null); return; }
            done({ tagId: j[0].id, tag: j[0].slug,
                   title: decodeEntities(j[0].name), count: j[0].count });
        });
}

// ── the shelf: every release post under a tag, cover + year + size parsed out. ──
// Weekly packs ("… Week 27.2026" dumps) are noise in a series view — filtered.
function mapPosts(j) {
    var out = [];
    for (var i = 0; i < j.length; i++) {
        var p = j[i];
        var title = decodeEntities(p.title && p.title.rendered);
        if (/weekly pack|week \d+\.\d{4}|^\d{4}\.\d{2}\.\d{2}/i.test(title)) continue;
        var ex = String((p.excerpt && p.excerpt.rendered) || "");
        var ym = ex.match(/Year\s*:\s*<\/strong>\s*(\d{4})/i);
        var sm = ex.match(/Size\s*:\s*<\/strong>\s*([\d.]+)\s*(GB|MB)/i);
        var sizeMB = sm ? (parseFloat(sm[1]) * (sm[2].toUpperCase() === "GB" ? 1024 : 1)) : 0;
        var og = p.yoast_head_json && p.yoast_head_json.og_image
                 && p.yoast_head_json.og_image[0] ? p.yoast_head_json.og_image[0].url : "";
        // strip the synopsis: text after the Year|Size line, tags removed
        var syn = decodeEntities(ex.replace(/<p[^>]*>.*?Size\s*:.*?<\/p>/i, "")
                                   .replace(/<[^>]+>/g, "")).trim();
        out.push({
            id: String(p.id), url: p.link, name: title,
            cover: og ? og.replace(/^http:/, "https:") : "",
            year: ym ? Number(ym[1]) : 0,
            sizeMB: Math.round(sizeMB),
            synopsis: syn,
            date: p.date || "",
            // TPB / omnibus / treasury / hardcover reads as a COLLECTION;
            // plain "#N" posts are single issues.
            collection: /\(TPB\)|omnibus|treasury|library edition|hardcover|complete collection/i.test(title)
        });
    }
    return out;
}

// WP caps a request at 100 posts, so big series (Walking Dead = 223) span pages.
// Page 1 is handed back immediately (fast first paint); the rest fetch in
// parallel and `done` fires once more with the FULL list, order preserved.
// done(list, totalOnSite) — totalOnSite > list.length only while loading or
// past the 5-page (500-release) sanity cap.
var MAX_PAGES = 5;

function releases(tagId, done) {
    var base = GC + "/posts?tags=" + tagId + "&per_page=100"
             + "&_fields=id,link,title,date,excerpt,yoast_head_json.og_image";
    gcJson(base + "&page=1", function(j, totalPages, total) {
        if (!j || !j.length) { done([], 0); return; }
        var first = mapPosts(j);
        var pages = Math.min(totalPages || 1, MAX_PAGES);
        if (pages <= 1) { done(first, total || first.length); return; }
        done(first, total || 0);                      // paint page 1 now
        var slots = new Array(pages - 1), pending = pages - 1;
        for (var p = 2; p <= pages; p++) (function(pg) {
            gcJson(base + "&page=" + pg, function(jp) {
                slots[pg - 2] = (jp && jp.length) ? mapPosts(jp) : [];
                pending -= 1;
                if (pending > 0) return;
                var all = first;
                for (var s = 0; s < slots.length; s++) all = all.concat(slots[s]);
                done(all, total || all.length);       // the full, honest shelf
            });
        })(p);
    });
}

// ── Explore board: GetComics' REAL taxonomy — publishers + franchises, live counts. ──
// GetComics has no genre axis (ratified diagnosis 2026-07-04, Hemanth picked this
// board over a genre facade): its archive is organized by publisher and franchise
// tags. Top tags by count, noise filtered, classified, iTunes art trailing in.
// `done` fires with the boxes immediately, then again as covers land.
var NOISE_TAG = /^(0-day|non 0-day|tpb|request|getcomics|.*\bweek\b.*|marvel now|infinity comic|dc comics collection|the art of|epic collection|zip)$/i;
var PUBLISHER_TAG = /^(marvel comics|dc comics|image comics|idw|boom studios|dynamite entertainment|archie|vertigo|zenescope|oni press|valiant|mad cave|aftershock comics|rebellion|europe comics|dark horse|titan comics|avatar press|vault comics|black mask|ahoy comics|action lab)$/i;

var exploreCache = null;

function explore(done) {
    if (exploreCache) { done(exploreCache); return; }
    gcJson(GC + "/tags?per_page=60&orderby=count&order=desc&_fields=id,name,slug,count",
        function(j) {
            if (!j || !j.length) { done([]); return; }
            var pubs = [], frans = [];
            for (var i = 0; i < j.length; i++) {
                var name = decodeEntities(j[i].name);
                if (NOISE_TAG.test(name)) continue;
                var box = { name: name, count: j[i].count, tag: j[i].slug, tagId: j[i].id,
                            cover: "", c1: "#6a4a32", c2: "#241813" };
                if (PUBLISHER_TAG.test(name)) pubs.push(box);
                else frans.push(box);
            }
            var boxes = pubs.slice(0, 8).concat(frans.slice(0, 12));
            exploreCache = boxes;
            done(boxes);
            boxes.forEach(function(box, idx) {
                posterFor(box.name + " comic", function(art) {
                    if (!art.length) return;
                    var copy = exploreCache.slice()
                    if (copy[idx]) { copy[idx].cover = art; exploreCache = copy; done(copy); }
                });
            });
        });
}

// ── iTunes: series-level poster art (600×600 from the 100×100 thumb URL). ──
// Session-cached per term — search fires per keystroke and iTunes rate-limits.
var posterCache = {};   // term → url ("" = looked up, none found)

function posterFor(term, done) {
    var key = String(term || "").toLowerCase();
    if (!key) { done(""); return; }
    if (posterCache[key] !== undefined) { done(posterCache[key]); return; }
    var url = ITUNES + "?term=" + encodeURIComponent(term) + "&media=ebook&limit=1";
    reqJson(url, function(j) {
        var art = "";
        if (j && j.results && j.results.length && j.results[0].artworkUrl100)
            art = String(j.results[0].artworkUrl100).replace("100x100", "600x600");
        posterCache[key] = art;
        done(art);
    });
}

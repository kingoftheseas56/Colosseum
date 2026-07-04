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

function reqJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

// like reqJson but hands back WP's pagination headers (exposed via CORS):
// done(json, totalPages, totalItems)
function reqJsonPaged(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null, 0, 0); return; }
        var tp  = Number(xhr.getResponseHeader("X-WP-TotalPages") || 0);
        var tot = Number(xhr.getResponseHeader("X-WP-Total") || 0);
        try { done(JSON.parse(xhr.responseText), tp, tot); } catch (e) { done(null, 0, 0); }
    };
    xhr.open("GET", url);
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
function searchSeries(query, done) {
    var q = encodeURIComponent(query.trim());
    reqJson(GC + "/tags?search=" + q + "&per_page=20&orderby=count&order=desc"
            + "&_fields=id,name,slug,count", function(j) {
        if (!j || !j.length) { done([]); return; }
        var toks = query.trim().toLowerCase().split(/\s+/).filter(function(t) { return t.length > 1 });
        var out = [];
        for (var i = 0; i < j.length; i++) {
            if (!j[i].count) continue;                       // empty tag = no releases
            var name = decodeEntities(j[i].name);
            var nl = name.toLowerCase(), hits = 0;
            for (var t = 0; t < toks.length; t++)
                if (nl.indexOf(toks[t]) >= 0) hits += 1;
            out.push({ tagId: j[i].id, tag: j[i].slug, title: name,
                       count: j[i].count, _hits: hits });
        }
        out.sort(function(a, b) { return (b._hits - a._hits) || (b.count - a.count); });
        done(out.slice(0, 6));
    });
}

function tagBySlug(slug, done) {
    reqJson(GC + "/tags?slug=" + encodeURIComponent(slug) + "&_fields=id,name,slug,count",
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
    reqJsonPaged(base + "&page=1", function(j, totalPages, total) {
        if (!j || !j.length) { done([], 0); return; }
        var first = mapPosts(j);
        var pages = Math.min(totalPages || 1, MAX_PAGES);
        if (pages <= 1) { done(first, total || first.length); return; }
        done(first, total || 0);                      // paint page 1 now
        var slots = new Array(pages - 1), pending = pages - 1;
        for (var p = 2; p <= pages; p++) (function(pg) {
            reqJsonPaged(base + "&page=" + pg, function(jp) {
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

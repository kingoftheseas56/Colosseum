// XoxoApi.js — the XOXO comics source (xoxocomic.com): catalog + per-issue page reading.
// Peer of ComicsApi (GetComics) behind the ComicSources registry — the Cinemeta/Kitsu
// model, no cross-source merge. All endpoints + parsing anchors proven live 2026-07-09:
// docs/superpowers/specs/2026-07-09-colosseum-xoxocomic-feasibility.md.
// Maintenance insurance: keiyoushi/extensions-source src/en/xoxocomics (WPComics engine)
// is the proven parsing contract — when xoxo changes markup, read their diff.
.pragma library

// ONE base-url constant — the domain has drifted once already (xoxocomics.com → xoxocomic.com).
var BASE = "https://xoxocomic.com";
var UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

// ── polite serialized fetch queue (same discipline as ComicsApi's gcPump) ──
var _q = [];
var _busy = false;
function fetchText(url, done) { _q.push({ url: url, done: done }); _pump(); }
function _pump() {
    if (_busy || _q.length === 0) return;
    _busy = true;
    var job = _q.shift();
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var text = (xhr.status >= 200 && xhr.status < 300) ? xhr.responseText : null;
        _busy = false;
        _pump();
        job.done(text);
    };
    xhr.open("GET", job.url);
    xhr.setRequestHeader("User-Agent", UA);   // CachingNam respects a caller-set UA
    xhr.send();
}

// ── id helpers: everything public is namespaced "xoxo:" so no id can collide
//    with manga chapters or GetComics issues (separate Continue entries for free) ──
function slugOf(id) { return String(id).replace(/^xoxo:/, ""); }
function slugTitle(slug) {
    return slug.split("-").map(function(w) {
        return w.length ? w.charAt(0).toUpperCase() + w.slice(1) : w;
    }).join(" ");
}

// ── pure parsers (fixture-tested by tests/xoxo_api_harness.qml — keep them pure) ──

// Series cards on search/genre/hot pages. Card-block scan: split on series anchors,
// then look INSIDE each card's slice for its title attr and lazy cover img. The card's
// image anchor carries title="<Name> Comic"; the site appends " Comic" — strip it.
function parseSeriesList(html) {
    var out = [];
    var seen = {};
    var re = /<a[^>]+href="https:\/\/xoxocomic\.com\/comic\/([a-z0-9-]+)"[^>]*>/g;
    var marks = [];
    var m;
    while ((m = re.exec(html)) !== null)
        marks.push({ slug: m[1], at: m.index, tag: m[0] });
    for (var i = 0; i < marks.length; i++) {
        var mk = marks[i];
        if (seen[mk.slug]) continue;
        seen[mk.slug] = true;
        var end = (i + 1 < marks.length) ? marks[i + 1].at : Math.min(html.length, mk.at + 2000);
        var block = html.slice(mk.at, end);
        var title = "";
        var tAttr = mk.tag.match(/title="([^"]+)"/);
        if (tAttr) title = decodeEntities(tAttr[1]).replace(/\s+Comic$/, "");
        if (!title) {
            var inner = block.match(/>([^<>{}]{2,120})<\/a>/);
            title = inner ? decodeEntities(inner[1].trim()) : "";
        }
        if (!title) title = slugTitle(mk.slug);
        var cover = "";
        var img = block.match(/data-original=['"]([^'"]+)['"]/) || block.match(/<img[^>]+src=['"](http[^'"]+(?:jpg|jpeg|png|webp)[^'"]*)['"]/);
        if (img) cover = img[1];
        out.push({ id: "xoxo:" + mk.slug, title: title, cover: cover });
    }
    return out;
}

// Issue rows on a series page (50/page) + the rel=next pagination link.
// Returns { issues: [{issueId, label, date}], nextUrl: ""|url }.
// Skips issue-1000000 (the site's magic "latest" alias — a phantom duplicate of the
// newest real issue) and the "Latest Issue" button (nested <i>, so [^<]* won't match it).
function parseIssueList(html, slug) {
    var issues = [];
    var seen = {};
    var re = new RegExp('<a[^>]+href="https://xoxocomic\\.com/comic/' + slug + '/(issue-[a-z0-9-]+)"[^>]*>([^<]*)</a>', "g");
    var m;
    while ((m = re.exec(html)) !== null) {
        var iss = m[1];
        if (iss === "issue-1000000") continue;   // magic "latest" alias — not a real issue
        if (seen[iss]) continue;
        seen[iss] = true;
        var label = decodeEntities(m[2].trim()) || iss.replace(/^issue-/, "Issue ");
        // the date rides a sibling cell (Keiyoushi: div.col-xs-3) — best-effort, "" is fine
        var tail = html.slice(m.index, m.index + 600);
        var d = tail.match(/(\d{2}\/\d{2}\/\d{4})/);
        issues.push({ issueId: "xoxo:" + slug + "/" + iss, label: label, date: d ? d[1] : "" });
    }
    var nextUrl = "";
    var n = html.match(/<a[^>]+rel="next"[^>]+href="([^"]+)"/) || html.match(/<a[^>]+href="([^"]+)"[^>]+rel="next"/);
    if (n) nextUrl = _abs(n[1]);
    return { issues: issues, nextUrl: nextUrl };
}

// Page images on an /issue-N/all reading page — data-original attrs are SINGLE-quoted.
function parsePages(html) {
    var urls = [];
    var re = /data-original='([^']+)'/g;
    var m;
    while ((m = re.exec(html)) !== null)
        if (m[1].indexOf("http") === 0) urls.push(m[1]);
    return urls;
}

// rel=next hrefs come relative ("batman-1940?page=2") — make them absolute.
function _abs(href) {
    if (/^https?:\/\//.test(href)) return href;
    if (href.indexOf("/") === 0) return BASE + href;
    return BASE + "/comic/" + href;
}

function decodeEntities(s) {
    return String(s).replace(/&amp;/g, "&").replace(/&#0?39;/g, "'").replace(/&quot;/g, '"')
                    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&#8211;/g, "–");
}

// ── soft-block detection (spec A) ─────────────────────────────────────────────────
// A rate-limited xoxo answers ANY url with HTTP 200 + a homepage-shaped page (the throttle
// interstitial OR the real homepage) instead of the asked-for content. Detect the lie
// POSITIVELY — the response must contain what THIS request asked for; a homepage lacks it.
// Markers verified against captured fixtures (2026-07-09):
//   • listing pages (search/genre/explore) carry the `comic-filter` / `pagination-outter`
//     chrome; the homepage has NEITHER (this holds even for a zero-result search — the
//     filter bar is page chrome, not results).
//   • a series page carries `/comic/<slug>/issue-` links for the REQUESTED slug.
//   • a reading page carries `data-original` images under the REQUESTED `<slug>/issue-N`.
// No title fingerprint — the throttle interstitial and the real homepage use DIFFERENT
// titles, so positive content markers are the only robust signal.
function isSoftBlock(html, verb, slug) {
    if (!html || html.length < 500) return true;
    if (verb === "search" || verb === "explore")
        return html.indexOf("comic-filter") < 0 && html.indexOf("pagination-outter") < 0;
    if (verb === "issues")
        return html.indexOf("/comic/" + slug + "/issue-") < 0;
    if (verb === "pages") {
        var re = new RegExp("data-original='[^']*" + String(slug).replace(/\//g, "\\/") + "\\/[0-9]+\\.");
        return !re.test(html);
    }
    return false;
}

// ── the 5-verb source contract (spec: xoxo-getcomics-design.md) ──

function searchSeries(query, done) {
    fetchText(BASE + "/search-comic?keyword=" + encodeURIComponent(query), function(html) {
        done(html ? parseSeriesList(html) : []);
    });
}

// Browse boxes: xoxo's REAL genre axis (stable slugs proven in the feasibility probe)
// + the two dynamic shelves. GetComics never had genres — this is the upgrade.
var GENRES = ["superhero", "sci-fi", "horror", "marvel", "dc-comics", "dark-horse",
              "graphic-novels", "movies-tv", "leading-ladies", "comedy", "mystery",
              "robots", "zombies"];
function explore(done) {
    var boxes = [{ id: "hot-comic", label: "Popular", kind: "shelf" },
                 { id: "comic-update", label: "Latest Updates", kind: "shelf" }];
    GENRES.forEach(function(g) { boxes.push({ id: g + "-comic", label: slugTitle(g), kind: "genre" }); });
    done(boxes);
}

// One box's series, paginated. Overflow quirk (proven): pages past the real end
// REPEAT earlier content — the CALLER stops when a page's first id equals the
// previous page's first id (see XoxoGenrePage). hasMore here = a next link exists.
function exploreItems(boxId, page, done) {
    var url = BASE + "/" + boxId + (page > 1 ? "?page=" + page : "");
    fetchText(url, function(html) {
        if (!html) { done({ items: [], hasMore: false }); return; }
        var items = parseSeriesList(html);
        var hasNext = /rel="next"/.test(html);
        done({ items: items, hasMore: hasNext && items.length > 0 });
    });
}

// FULL issue list — walks rel=next recursively (Batman 1940 spans 7+ pages of 50).
// Safety cap 40 pages (2,000 issues) so a parser regression can't loop forever.
function issues(seriesId, done) {
    var slug = slugOf(seriesId);
    var all = [];
    var hops = 0;
    function walk(url) {
        fetchText(url, function(html) {
            if (!html) { done(all); return; }
            var r = parseIssueList(html, slug);
            all = all.concat(r.issues);
            hops += 1;
            if (r.nextUrl && hops < 40) walk(r.nextUrl);
            else done(all);
        });
    }
    walk(BASE + "/comic/" + slug);
}

// Full-resolution ordered page URLs for one issue — feeds Downloads.downloadPages.
function pages(issueId, done) {
    var path = slugOf(issueId);   // "batman-1940/issue-1"
    fetchText(BASE + "/comic/" + path + "/all", function(html) {
        done(html ? parsePages(html) : []);
    });
}

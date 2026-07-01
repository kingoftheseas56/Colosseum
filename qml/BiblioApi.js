// BiblioApi.js - tiny live catalog adapter for the Colosseum Biblio (books) world.
// Apple Books charts are the discovery/identity source: a daily-fresh Top chart gives the
// Featured carousel + the Top-10 row, with hi-res cover art on the SAME mzstatic CDN the comics
// world already uses (so the native launcher's disk cache handles it). Genres are static in
// Catalog.js (mirrors Theatre: it live-fetches rows, not the genre tiles). Delivery stays libgen
// (search/download) - separate layer, exactly like Cinemeta vs the Theatre stream addon.
//
// Two Apple gotchas this file guards against (both verified live, 2026-06-27):
//   1. The store blocks requests with no descriptive User-Agent. QML's XMLHttpRequest sends one
//      by default, so the live path is fine; curl/scripts must set -A.
//   2. The RSS `entry` field is a single OBJECT when the feed has one result, an ARRAY when many.
//      entriesOf() always normalizes to an array.
.pragma library
.import "BiblioLookupSelector.js" as Selector

var COUNTRY = "us";
var FEED = "https://itunes.apple.com/" + COUNTRY + "/rss/topebooks";

// warm, book-ish tints (lift / shadow) used while a cover loads — same role as Theatre's palette
var palette = [
    ["#6a4a2c", "#1d1209"],
    ["#5a3a3f", "#180d10"],
    ["#3f5a4a", "#101a14"],
    ["#4a4063", "#13101f"],
    ["#6a5a2c", "#1d1809"],
    ["#3f4a63", "#0e121b"],
    ["#5a4a3a", "#181210"],
    ["#634050", "#1b0d14"],
    ["#3a5a5a", "#0e1a1a"],
    ["#5a5a3a", "#181810"]
];

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE)
            return;
        if (xhr.status < 200 || xhr.status >= 300) {
            done(null);
            return;
        }
        try {
            done(JSON.parse(xhr.responseText));
        } catch (e) {
            done(null);
        }
    };
    xhr.open("GET", url);
    xhr.send();
}

// Apple RSS `entry`: single object for 1 result, array for many. Always hand back an array.
function entriesOf(json) {
    if (!json || !json.feed || !json.feed.entry)
        return [];
    var e = json.feed.entry;
    return (e instanceof Array) ? e : [e];
}

function labelOf(node) {
    return (node && node.label) ? node.label : "";
}

// im:image is [{ label, attributes:{height} }] small->large; take the largest, then ask the
// mzstatic thumb service for a clean portrait cover (preserves aspect, books are tall).
function coverOf(entry) {
    var imgs = entry["im:image"];
    if (!imgs || imgs.length === 0)
        return "";
    var url = String(imgs[imgs.length - 1].label);
    return url.replace(/\/[0-9]+x[0-9]+bb\.(png|jpg|jpeg)$/, "/400x600bb.jpg");
}

function tone(index) {
    return palette[index % palette.length];
}

function mapBook(entry, index) {
    var t = tone(index);
    var name = labelOf(entry["im:name"]) || "Untitled";
    var author = labelOf(entry["im:artist"]);
    var cover = coverOf(entry);
    return {
        caption: name,
        title: name,
        author: author,
        blurb: author ? ("By " + author + ".") : "On the Apple Books chart.",
        cover: cover,
        art: cover,
        artKind: "poster",   // books are portrait covers, not wide banners
        ghost: "B",
        c1: t[0],
        c2: t[1],
        progress: -1
    };
}

// Base load: ONE request. The overall Top ebooks chart feeds both the Featured carousel
// (first few) and the Top-10 row. Genres stay static (Catalog.biblioGenres).
function loadBiblio(done) {
    requestJson(FEED + "/limit=12/json", function(json) {
        var mapped = entriesOf(json).map(mapBook);
        done({
            featured: mapped.slice(0, 4),
            top: mapped.slice(0, 10)
        });
    });
}

// ───────────────────────────────────────────────────────────────────────────
// DETAIL + SEARCH — full book objects from the iTunes Search API (it carries the
// rich fields the chart RSS lacks: description, rating, genres). One mapper feeds
// both the search page and tile→detail lookups.
// ───────────────────────────────────────────────────────────────────────────

function stripHtml(s) {
    return String(s || "")
        .replace(/<[^>]+>/g, "")
        .replace(/&#xa0;|&nbsp;/gi, " ")
        .replace(/&amp;/gi, "&").replace(/&quot;/gi, '"')
        .replace(/&#x27;|&#39;|&apos;/gi, "'")
        .replace(/&#x2014;|&mdash;/gi, "—").replace(/&#x2013;|&ndash;/gi, "–")
        .replace(/&[#a-z0-9]+;/gi, " ")          // any remaining entity → space
        .replace(/([a-z”'’"])([.!?])([A-Z“"‘])/g, "$1$2 $3")   // "marriage.Hugo" → "marriage. Hugo"
        .replace(/\s+/g, " ").trim();
}

function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s; }

// Publisher blurbs lead with marketing noise (bestseller badges, film tie-ins, copy counts,
// asterisks). Scrub it so neither the tagline nor the drop-capped synopsis opens on a shout.
function cleanBlurb(s) {
    var d = stripHtml(s).replace(/\*+/g, " ");
    var noise = [
        /(the\s+)?(instant\s+)?#?\s*1?\s*(international|new york times|usa today|sunday times|national|wall street journal)\s+bestsell\w*/ig,
        /(instant\s+)?#?\s*1\s+bestsell\w*/ig,
        /now a (major )?(motion picture|netflix|hbo|apple tv\+?|prime video|major series|tv series|film)[^.!?]*/ig,
        /soon to be[^.!?]*/ig,
        /over [\d,.]+\s*(million|thousand)?\s*copies sold[^.!?]*/ig,
        /translated into[^.!?]*/ig,
        /(a|an)?\s*(reese'?s|oprah'?s|today show|good morning america|gma|jenna'?s)\s*book club( pick)?/ig,
        /(winner|finalist)\s+(of|for)\b[^.!?]*/ig,
        /\b\d+\s+best books?\b[^.!?]*/ig,
        /\bhugo award[^.!?]*/ig,
        /\b(reader'?s?|readers?')\s+(pick|favorite)\b[^.!?]*/ig,
        /\bworldwide phenomenon\b[^.!?]*/ig,
        /\bnational book award[^.!?]*/ig,
        /named (a|one of)[^.!?]*best book[^.!?]*/ig
    ];
    for (var i = 0; i < noise.length; i++) d = d.replace(noise[i], " ");
    return d.replace(/^[\s•\-–—:#"'.,!]+/, "").replace(/\s{2,}/g, " ").trim();
}

// a candidate tagline that still smells of marketing (or SHOUTS in caps) isn't a hook — drop it
function looksMarketing(s) {
    if (/bestsell|motion picture|book club|copies sold|award|netflix|oprah|reese|#\s*1\b/i.test(s)) return true;
    var caps = (s.match(/[A-Z]/g) || []).length, letters = (s.match(/[a-zA-Z]/g) || []).length;
    return letters > 0 && caps / letters > 0.5;
}

// Lift the hero tagline out of the (cleaned) blurb. HIGH PRECISION ONLY: a clean hook clause that
// follows a colon — "…award-winning author X: the girl who wouldn't die…. Story…" — which also strips
// the marketing preamble so the synopsis opens at the story. Any other shape → no tagline (the layout
// omits it gracefully). Better an honest title-page with no tagline than a garbage one.
// Keep the synopsis to a readable paragraph — publisher blurbs run to half a page. Cut at a sentence
// boundary near the cap, else a word boundary, and add an ellipsis.
function clampSynopsis(s, max) {
    s = String(s || "").trim();
    if (s.length <= max) return s;
    var cut = s.substring(0, max);
    var end = Math.max(cut.lastIndexOf(". "), cut.lastIndexOf("! "), cut.lastIndexOf("? "));
    if (end > max * 0.55) return cut.substring(0, end + 1).trim();
    var sp = cut.lastIndexOf(" ");
    return cut.substring(0, sp > 0 ? sp : max).trim() + "…";
}

function splitTagline(desc) {
    var d = cleanBlurb(desc);
    if (!d) return { tagline: "", body: "" };
    var colon = d.match(/^[^.!?:]{8,170}:\s+([A-Za-z“"'][^:;]{14,130}?[.!?])\s+(.+)$/);
    if (colon) {
        var hook = colon[1].trim();
        if (!looksMarketing(hook) && !/\.\.\.$/.test(hook))
            return { tagline: cap(hook), body: colon[2].trim() };
    }
    return { tagline: "", body: d };
}

// hi-res portrait cover for the detail hero
function bigCover(url) {
    if (!url) return "";
    return String(url).replace(/\/[0-9]+x[0-9]+bb\.(png|jpg|jpeg)$/, "/600x900bb.jpg");
}

function genreLine(genres, author, year) {
    var gs = (genres || []).filter(function(g) { return g !== "Books"; });
    var parts = [];
    if (gs.length) parts.push(gs[0]);
    if (author) parts.push(author);
    if (year) parts.push(year);
    return parts.join("  ·  ");
}

// one iTunes Search ebook result → the shape BiblioBook.qml renders
function fullBook(r) {
    var split = splitTagline(r.description);
    var year = String(r.releaseDate || "").substring(0, 4);
    return {
        id: r.trackId,
        title: r.trackName || r.trackCensoredName || "Untitled",
        author: r.artistName || "",
        year: year,
        genres: (r.genres || []).filter(function(g) { return g !== "Books"; }),
        genreLine: genreLine(r.genres, r.artistName, year),
        tagline: split.tagline,
        synopsis: clampSynopsis(split.body || stripHtml(r.description), 400),
        cover: bigCover(r.artworkUrl100 || r.artworkUrl60 || ""),
        rating: r.averageUserRating || 0,
        ratingCount: r.userRatingCount || 0
    };
}

// live search → array of full book objects (powers the search page AND tile→detail)
function search(query, done) {
    if (!query) { done([]); return; }
    var url = "https://itunes.apple.com/search?media=ebook&limit=24&term=" + encodeURIComponent(query);
    requestJson(url, function(json) {
        var results = (json && json.results) ? json.results : [];
        done(results.map(fullBook));
    });
}

// open-by-title: first match's full detail (so a home tile with only a title can open a detail)
function lookupBook(title, author, done) {
    if (typeof author === "function") {
        done = author
        author = ""
    }
    search(title, function(books) {
        done(Selector.pickBookMatch(books, title, author || ""));
    });
}

// ───────────────────────────────────────────────────────────────────────────
// LIBGEN — the delivery layer. Recreates TB2's LibGenScraper (C++) in QML JS:
// search libgen.li by title+author, scrape the result table, list the available
// editions (format · size · year · language + md5). Opening an edition links to its
// libgen page to download (full in-app streaming download needs the native engine —
// a follow-up). libgen.li verified reachable 2026-06-27 (.is/.rs are not, here).
// ───────────────────────────────────────────────────────────────────────────

var LIBGEN = "https://libgen.li";
var LIBGEN_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

function requestText(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        done(xhr.responseText);
    };
    xhr.open("GET", url);
    try { xhr.setRequestHeader("User-Agent", LIBGEN_UA); } catch (e) {}
    xhr.send();
}

function cellsOf(rowHtml) {
    var cells = [], re = /<td[^>]*>([\s\S]*?)<\/td>/gi, m;
    while ((m = re.exec(rowHtml)) !== null) cells.push(m[1]);
    return cells;
}

// search libgen for a book → its available editions (one row = one downloadable file)
// columns (verified 2026-06-27): [3]=year [4]=language [6]=size [7]=extension [8]=mirrors(md5)
function searchLibgen(title, author, done) {
    var term = (title || "") + (author ? " " + author : "");
    if (!term.trim()) { done([]); return; }
    var url = LIBGEN + "/index.php?req=" + encodeURIComponent(term) + "&topics%5B%5D=l&topics%5B%5D=f";
    requestText(url, function(html) {
        if (!html) { done([]); return; }
        var tm = html.match(/<table[^>]*id="tablelibgen"[^>]*>([\s\S]*?)<\/table>/i);
        if (!tm) { done([]); return; }
        var body = tm[1].replace(/<(br|wbr|hr)\s*\/?>/gi, " ");
        var rows = body.match(/<tr[^>]*>[\s\S]*?<\/tr>/gi) || [];
        var out = [], bestSet = false;
        for (var i = 0; i < rows.length; i++) {
            var md5m = rows[i].match(/(?:ads|get)\.php\?[^"']*md5=([a-fA-F0-9]{32})/i);
            if (!md5m) continue;                       // skips the header row
            var c = cellsOf(rows[i]);
            if (c.length < 9) continue;
            var ed = {
                md5: md5m[1],
                format: stripHtml(c[7]).toLowerCase(),
                size: stripHtml(c[6]),
                year: stripHtml(c[3]),
                language: stripHtml(c[4]),
                detailUrl: LIBGEN + "/ads.php?md5=" + md5m[1],
                best: false
            };
            if (!bestSet && ed.format === "epub") { ed.best = true; bestSet = true; }
            out.push(ed);
            if (out.length >= 12) break;
        }
        if (!bestSet && out.length > 0) out[0].best = true;
        done(out);
    });
}

// ───────────────────────────────────────────────────────────────────────────
// FICTIONDB SERIES — recreates TB2's BookCatalogueAggregator (C++) in QML JS.
// FictionDB has no browsable series directory, but every book page DECLARES its
// series ("Legends of Dune - 4"). So series are reached THROUGH the books: search →
// fetch the top-8 book pages → read each one's series line → group books that share a
// series id. This is the only path that gives the SERIES row (Apple has no series
// field). fictiondb.com verified reachable 2026-06-27. Two-pass = a few seconds.
// ───────────────────────────────────────────────────────────────────────────

var FICTIONDB = "https://www.fictiondb.com";

function parseFdbSearch(html) {
    var books = [], rowRe = /<tr[^>]*itemtype="[^"]*schema\.org\/Book"[^>]*>([\s\S]*?)<\/tr>/gi, m;
    while ((m = rowRe.exec(html)) !== null) {
        var um = m[1].match(/itemprop="url"\s+href="\.\.\/title\/([a-z0-9-]+~[a-z0-9-]*~\d+)\.htm"/i);
        if (!um) continue;
        var nm = m[1].match(/itemprop=['"]name['"]>\s*([^<]+?)\s*<\/span>/i);
        var am = m[1].match(/itemprop="author"[^>]*>\s*([^<]+?)\s*<\/a>/i);
        books.push({ slug: um[1], title: nm ? stripHtml(nm[1]) : "", author: am ? am[1].trim() : "" });
    }
    return books;
}

function parseFdbBook(html, slug) {
    var ogt = html.match(/<meta\s+property="og:title"\s+content="([^"]*)"/i);
    var ogi = html.match(/<meta\s+property="og:image"\s+content="([^"]*)"/i);
    var title = "", author = "";
    if (ogt) {
        var t = stripHtml(ogt[1]);                    // "Paul of Dune by Brian Herbert; Kevin J. Anderson"
        var by = t.lastIndexOf(" by ");
        if (by > 0) { title = t.substring(0, by).trim(); author = t.substring(by + 4).trim(); }
        else title = t;
    }
    var ser = html.match(/href="\.\.\/series\/([a-z0-9-]+~\d+)\.htm"[^>]*>\s*([^<]+?)\s*-\s*(\d+)\s*</i);
    return {
        slug: slug, title: title, author: author,
        cover: ogi ? ogi[1] : "",
        seriesId: ser ? ser[1] : "",
        seriesName: ser ? stripHtml(ser[2]) : "",
        position: ser ? parseInt(ser[3], 10) : 0
    };
}

// search FictionDB → the series among the top-N results (TB2's two-pass grouping)
function searchFictionSeries(query, done) {
    if (!query || query.trim().length < 2) { done([]); return; }
    requestText(FICTIONDB + "/search/searchresults.htm?srchtxt=" + encodeURIComponent(query) + "&styp=5", function(html) {
        if (!html) { done([]); return; }
        var flat = parseFdbSearch(html).slice(0, 8);
        if (flat.length === 0) { done([]); return; }
        var resolved = [], pending = flat.length;
        function finish() {
            pending -= 1;
            if (pending > 0) return;
            var groups = [], idx = {};
            for (var i = 0; i < resolved.length; i++) {
                var b = resolved[i];
                if (!b || !b.seriesId || !b.seriesName) continue;     // requires a declared series
                if (!(b.seriesId in idx)) {
                    idx[b.seriesId] = groups.length;
                    groups.push({ seriesId: b.seriesId, seriesName: b.seriesName, author: b.author, cover: b.cover, books: [] });
                }
                groups[idx[b.seriesId]].books.push({ title: b.title, position: b.position, slug: b.slug, cover: b.cover });
            }
            for (var g = 0; g < groups.length; g++) {
                groups[g].books.sort(function(a, c) { return a.position - c.position; });
                groups[g].count = groups[g].books.length;
            }
            done(groups);
        }
        for (var j = 0; j < flat.length; j++) {
            (function(slug) {
                requestText(FICTIONDB + "/title/" + slug + ".htm", function(bhtml) {
                    resolved.push(bhtml ? parseFdbBook(bhtml, slug) : null);
                    finish();
                });
            })(flat[j].slug);
        }
    });
}

// fetch ALL books in a series (its page lists them in reading order) — thin: slug + position only
function fetchSeriesBooks(seriesId, done) {
    requestText(FICTIONDB + "/series/" + seriesId + ".htm", function(html) {
        if (!html) { done({ seriesName: "", books: [] }); return; }
        var name = "";
        var h1 = html.match(/<h1[^>]*>\s*([^<]+?)\s*</i);
        if (h1) name = stripHtml(h1[1]);
        var books = [], seen = {}, re = /href="\.\.\/title\/([a-z0-9-]+~[a-z0-9-]*~\d+)\.htm"/gi, m, pos = 1;
        while ((m = re.exec(html)) !== null) {
            if (seen[m[1]]) continue;
            seen[m[1]] = true;
            books.push({ slug: m[1], position: pos++ });
        }
        done({ seriesName: name, books: books });
    });
}

// "the-road-to-dune~brian-herbert~id" → "The Road to Dune" (title-case, stopwords lower)
function titleFromSlug(slug) {
    var part = String(slug || "").split("~")[0];
    var lower = { of:1, the:1, and:1, to:1, a:1, in:1, for:1, on:1, at:1 };
    return part.split("-").filter(Boolean).map(function(w, i) {
        return (i > 0 && lower[w]) ? w : (w.charAt(0).toUpperCase() + w.slice(1));
    }).join(" ");
}

// The full series roster: every book in the series, in order, with covers. The series page gives the
// complete list (slug + position) but no covers; the books already found in search carry covers, and
// the rest get their cover fetched from their book page. Capped so a huge series doesn't hammer FictionDB.
function loadFullSeries(seriesId, knownBooks, done) {
    var known = {};
    (knownBooks || []).forEach(function(b) { if (b.slug) known[b.slug] = b; });
    fetchSeriesBooks(seriesId, function(res) {
        var members = res.books.slice(0, 30);
        if (members.length === 0) { done({ seriesName: res.seriesName, books: knownBooks || [] }); return; }
        var out = members.map(function(m) {
            var k = known[m.slug];
            return { slug: m.slug, position: m.position,
                     title: k ? k.title : titleFromSlug(m.slug),
                     cover: k ? k.cover : "" };
        });
        var missing = out.filter(function(b) { return !b.cover; });
        if (missing.length === 0) { done({ seriesName: res.seriesName, books: out }); return; }
        var pending = missing.length;
        missing.forEach(function(b) {
            requestText(FICTIONDB + "/title/" + b.slug + ".htm", function(html) {
                if (html) {
                    var parsed = parseFdbBook(html, b.slug);
                    b.cover = parsed.cover;
                    if (parsed.title) b.title = parsed.title;
                }
                pending -= 1;
                if (pending === 0) done({ seriesName: res.seriesName, books: out });
            });
        });
    });
}

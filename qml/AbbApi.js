// AbbApi.js — AudioBookBay scraper, ported from Tankoban 2's AbbScraper (C++ → QML JS).
// ABB supplies source postings, browse pages and upload facts, never canonical
// work or recording identity. Legacy title/author delivery remains available.
// Base URL oscillates on takedowns — ONE constant to bump.
// Regexes are verbatim from AbbScraper.cpp (verified against the 2026-04-22 probe fixtures).
.pragma library

var ABB = "https://audiobookbay.lu";
var ABB_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Qt QML XMLHttpRequest does not expose a standard timeout property.
function requestTimer(callback, timeoutMs) {
    if (typeof Qt !== "undefined" && Qt.createQmlObject && Qt.application) {
        var timer = Qt.createQmlObject(
            "import QtQml\nTimer { repeat: false }", Qt.application, "AbbRequestTimeout.qml");
        if (!timer) return null;
        timer.interval = timeoutMs;
        timer.triggered.connect(callback);
        timer.start();
        return function() { timer.stop(); timer.destroy(); };
    }
    if (typeof setTimeout === "function" && typeof clearTimeout === "function") {
        var id = setTimeout(callback, timeoutMs);
        return function() { clearTimeout(id); };
    }
    return null;
}

// done(html, reason) remains backwards compatible with existing done(html) callers.
// cancel() suppresses callbacks on teardown; cancel(true) reports "cancelled".
function requestText(url, done, options) {
    options = options || {};
    var ms = Number(options.timeoutMs);
    if (!(ms > 0) || !isFinite(ms)) ms = 12000;
    var xhr = new XMLHttpRequest();
    var finished = false, stopTimer = null;
    function finish(html, reason, suppress) {
        if (finished) return;
        finished = true;
        if (stopTimer) { stopTimer(); stopTimer = null; }
        if (!suppress) done(html, reason);
    }
    var handle = {
        cancel: function(notify) {
            if (finished) return;
            finish(null, "cancelled", !notify);
            xhr.onreadystatechange = null;
            try { xhr.abort(); } catch (e) {}
        }
    };
    xhr.onreadystatechange = function() {
        if (finished || xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status === 0) { finish(null, "offline"); return; }
        if (xhr.status < 200 || xhr.status >= 300) {
            finish(null, "http-" + xhr.status); return;
        }
        finish(xhr.responseText, null);
    };
    try {
        stopTimer = requestTimer(function() {
            finish(null, "timeout");
            xhr.onreadystatechange = null;
            try { xhr.abort(); } catch (e) {}
        }, ms);
        // Never start an unbounded request if the host cannot provide a timer.
        if (!stopTimer) { finish(null, "offline"); return handle; }
        xhr.open("GET", url);
        try { xhr.setRequestHeader("User-Agent", ABB_UA); } catch (e) {}
        xhr.send();
    } catch (e) {
        finish(null, "offline");
    }
    return handle;
}

function decode(s) {
    return String(s || "")
        .replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&apos;/g, "'")
        .replace(/&#039;/g, "'").replace(/&#8211;/g, "-").replace(/&#8212;/g, "-")
        .replace(/&#8230;/g, "...").replace(/&nbsp;/g, " ")
        .replace(/&lt;/g, "<").replace(/&gt;/g, ">").trim();
}

// Split search HTML into per-post blocks. EXACT match on `<div class="post">` — the
// honeypot decoys are `<div class="post re-ab" ...>`, which this excludes cleanly.
function isBundleTitle(title) {
    var text = String(title || "").trim();
    if (/\b(?:box\s*set|collection)\b/i.test(text)) return true;
    if (/\b(?:parts?|episodes?|chapters?|discs?)\s*#?\s*\d+\s*[-–—]\s*\d+\b/i.test(text))
        return false;
    var range = /(?:\b(?:books?|series|vol(?:ume)?s?)\s*#?\s*|#\s*)(\d{1,2})\s*[-–—]\s*(\d{1,2})\b/i.exec(text);
    if (!range) range = /(?:^|\s)(\d{1,2})\s*[-–—]\s*(\d{1,2})$/.exec(text);
    return !!range && Number(range[2]) > Number(range[1]);
}

function parseSearch(html) {
    var text = String(html || "");
    var starts = [], re = /<div class="post">/g, m;
    while ((m = re.exec(text)) !== null) starts.push(m.index);
    if (starts.length === 0) return [];
    var out = [];
    var titleRe = /<h2><a href="([^"]+)"[^>]*>([^<]+)<\/a><\/h2>/;
    var coverRe = /<img src="([^"]+)"[^>]*width="250"/;
    var langRe  = /Language:\s*([^<]+?)(?:<span|<br)/i;
    var fmtRe   = /Format:\s*<span[^>]*>([^<]+)<\/span>/i;
    var sizeRe  = /File Size:\s*<span[^>]*>([^<]+)<\/span>\s*([GMK]?Bs?)/i;
    var postedRe= /Posted:\s*([^<]+?)<br/i;
    var slugRe  = /\/abss\/([^/]+)\/?/;
    for (var i = 0; i < starts.length; i++) {
        var block = text.substring(starts[i], (i + 1 < starts.length) ? starts[i + 1] : text.length);
        var tm = titleRe.exec(block);
        if (!tm) continue;
        var href = tm[1].trim();
        var detailUrl = (href.indexOf("http") === 0) ? href : ABB + href;
        var sm = slugRe.exec(detailUrl);
        var slug = sm ? sm[1] : "";
        if (!slug) continue;
        var wt = decode(tm[2]);
        var title = wt, author = "";
        var dash = wt.lastIndexOf(" - ");
        if (dash > 0 && dash < wt.length - 3) {
            var after = wt.substring(dash + 3).trim();
            if (after && after.length <= 60 && after.indexOf("/") < 0 && after.indexOf("[") < 0 && !/^[0-9]/.test(after)) {
                author = after; title = wt.substring(0, dash).trim();
            }
        }
        var cm = coverRe.exec(block), lm = langRe.exec(block), fm = fmtRe.exec(block),
            zm = sizeRe.exec(block), pm = postedRe.exec(block);
        out.push({
            slug: slug, detailUrl: detailUrl, title: title, author: author,
            cover: cm ? cm[1].trim() : "",
            language: lm ? decode(lm[1]) : "",
            format: fm ? fm[1].trim() : "",
            size: zm ? (zm[1].trim() + " " + zm[2].trim()) : "",
            seeders: parseSeeders(block),
            posted: pm ? pm[1].trim() : "",
            bundle: isBundleTitle(title)
        });
    }
    return out;
}

function parseSeeders(html) {
    var row = /<td\b[^>]*>\s*Seeders:\s*<\/td>\s*<td\b[^>]*>\s*([\d,]+)\s*<\/td>/i.exec(html);
    var span = /Seeders:\s*<span\b[^>]*>\s*([\d,]+)\s*<\/span>/i.exec(html);
    var count = row || span;
    return count ? Number(count[1].replace(/,/g, "")) : undefined;
}

// Facts describe the ABB upload only; they never establish a catalogue match.
function detailSpan(html, name) {
    var re = new RegExp("<span\\s+class=[\"']" + name + "[\"'][^>]*>([\\s\\S]*?)<\\/span>", "i");
    var m = re.exec(html);
    return m ? decode(m[1].replace(/<[^>]+>/g, "")) : "";
}

function plainText(html) {
    return decode(String(html || "")
        .replace(/<script\b[\s\S]*?<\/script>/gi, "")
        .replace(/<style\b[\s\S]*?<\/style>/gi, "")
        .replace(/<br\s*\/?>/gi, "\n")
        .replace(/<\/p\s*>/gi, "\n")
        .replace(/<[^>]+>/g, "")
        .replace(/&#(x[0-9a-f]+|\d+);/gi, function(_, n) {
            var hex = n.substring(0, 1).toLowerCase() === "x";
            return String.fromCharCode(parseInt(hex ? n.substring(1) : n, hex ? 16 : 10));
        })
        .replace(/[ \t]+\n/g, "\n").replace(/\n{3,}/g, "\n\n")).trim();
}

function parseDetail(html) {
    var text = String(html || "");
    var hm = /<td>Info Hash:<\/td>\s*<td[^>]*>\s*([0-9a-fA-F]{40})\s*<\/td>/i.exec(text);
    var dm = /<div class=["']desc["'][^>]*>([\s\S]*?)<\/div>/i.exec(text);
    var desc = dm ? dm[1] : "";
    var header = /<p\b[^>]*>\s*Written by[\s\S]*?<\/p>/i.exec(desc);
    var heading = header ? header[0] : "";
    var authors = [], am, authorRe = /<span\s+class=["']author["'][^>]*>([^<]*)<\/span>/gi;
    while ((am = authorRe.exec(heading)) !== null) authors.push(decode(am[1]));
    var narrators = [], nm, narrRe = /<span\s+class=["']narrator["'][^>]*>([^<]*)<\/span>/gi;
    while ((nm = narrRe.exec(heading)) !== null) narrators.push(decode(nm[1]));
    var ab = detailSpan(heading, "is_abridged").toLowerCase();
    var abridgement = ab === "unabridged" ? "unabridged"
                     : ab === "abridged" ? "abridged"
                     : (ab === "dramatization" || ab === "dramatized") ? "dramatized" : "";
    var lang = /<span\b[^>]*itemprop=["']inLanguage["'][^>]*>([^<]+)<\/span>/i.exec(text);
    var posted = /<meta\s+itemprop=["']uploadDate["']\s+content=["']([^"']+)["']/i.exec(text);
    return {
        infoHash: hm ? hm[1].toLowerCase() : "",
        contents: parseFileSummary(text),
        author: authors.join(", "),
        narrator: narrators,
        format: detailSpan(heading, "format"),
        bitrate: detailSpan(heading, "bitrate"),
        abridgement: abridgement,
        language: lang ? decode(lang[1]) : "",
        description: plainText(desc.replace(/<p\b[^>]*>\s*Written by[\s\S]*?<\/p>/i, "")),
        postedDate: posted ? posted[1] : "",
        fileCount: parseFileCount(text),
        audioFileCount: parseFileCount(text, true),
        seeders: parseSeeders(text)
    };
}

// "Contents: N × .m4b, M × .mp3" — audio extensions first. Display + audio-file count.
function parseFileSummary(text) {
    var mk = /<tr>\s*<td[^>]*>\s*This is a (Multifile|Singlefile) Torrent\s*<\/td>\s*<\/tr>/i.exec(text);
    if (!mk) return "";
    var tail = text.substring(mk.index + mk[0].length, mk.index + mk[0].length + 32000);
    var fileRe = /<tr>\s*<td[^>]*>\s*([^<]+?)\.([a-zA-Z0-9]+)\s+([0-9.]+\s*[GMK]?Bs?)\s*<\/td>\s*<\/tr>/gi;
    var counts = {}, m, total = 0;
    while ((m = fileRe.exec(tail)) !== null && total < 200) {
        var ext = m[2].toLowerCase(); counts[ext] = (counts[ext] || 0) + 1; total++;
    }
    var order = ["m4b", "mp3", "m4a", "flac", "ogg", "wav"], parts = [], seen = {};
    for (var i = 0; i < order.length; i++) if (counts[order[i]]) { parts.push(counts[order[i]] + " × ." + order[i]); seen[order[i]] = 1; }
    var rest = Object.keys(counts).filter(function(k){ return !seen[k]; }).sort();
    for (var j = 0; j < rest.length; j++) parts.push(counts[rest[j]] + " × ." + rest[j]);
    return parts.length ? "Contents: " + parts.join(", ") : "";
}

// VERBATIM from AbbScraper::constructMagnet — ABB's own 7-tracker list (typo'd port
// kept; libtorrent ignores invalid trackers, matching ABB maximizes peer parity).
// File rows include covers and metadata; not a chapter count.
function parseFileCount(text, audioOnly) {
    var mk = /<tr>\s*<td[^>]*>\s*This is a (Multifile|Singlefile) Torrent\s*<\/td>\s*<\/tr>/i.exec(text);
    if (!mk) return 0;
    var tail = text.substring(mk.index + mk[0].length, mk.index + mk[0].length + 32000);
    var re = /<tr>\s*<td[^>]*>\s*([^<]+?)\.([a-zA-Z0-9]+)\s+([0-9.]+\s*[GMK]?Bs?)\s*<\/td>\s*<\/tr>/gi;
    var count = 0, total = 0, file;
    while ((file = re.exec(tail)) !== null && total++ < 200) {
        if (!audioOnly || /^(?:m4b|mp3|m4a|aac|flac|ogg|opus|wav|wma)$/i.test(file[2])) count++;
    }
    return count;
}

function constructMagnet(infoHash, title) {
    var dn = title ? encodeURIComponent(title) : "audiobookbay";
    return "magnet:?xt=urn:btih:" + infoHash + "&dn=" + dn
        + "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.open-internet.nl%3A6969%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A69691337%2Fannounce"
        + "&tr=udp%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.baravik.org%3A6970%2Fannounce"
        + "&tr=http%3A%2F%2Fretracker.telecom.by%3A80%2Fannounce"
        + "&tr=http%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce";
}

// The search term MUST go out LOWERCASE: ABB's ?s= REDIRECTS a query containing capital
// letters to the HOMEPAGE (reproduced deterministically, 3/3, 2026-07-18), and the homepage's
// latest posts then parse as plausible "results" — that is exactly how a "Joe Country" search
// downloaded a romance novel. Lowercase queries never redirect and return the real matches.
function searchTerm(title, author) {
    return ((title || "") + (author ? " " + author : "")).trim().toLowerCase();
}

// Words that can't establish relevance on their own — generic catalog noise that would let
// an unrelated post ("Al Clark: Book 3") count as a match for "The Book".
var STOP = { "the": 1, "and": 1, "for": 1, "with": 1, "book": 1, "series": 1,
             "audiobook": 1, "unabridged": 1, "novel": 1, "volume": 1 };

// The homepage-junk gate (defense in depth behind the lowercase fix): keep only rows whose
// title/author share at least one meaningful word with the requested book. If the site ever
// hands us an unrelated page again (redirect, mirror weirdness, no-match filler), the whole
// set collapses to [] and the caller shows "no audiobook found" — never strangers' uploads.
function relevantRows(rows, title, author) {
    var raw = ((title || "") + " " + (author || "")).toLowerCase().match(/[a-z0-9]{3,}/g) || [];
    var words = raw.filter(function(w) { return !STOP[w]; });
    if (words.length === 0) words = raw;   // stopword-only title ("The Book") → raw words are
                                           // the only signal left; better than pass/drop-all
    if (words.length === 0) return rows;   // no words at all → nothing to gate against
    var out = [];
    for (var i = 0; i < rows.length; i++) {
        var hay = ((rows[i].title || "") + " " + (rows[i].author || "")).toLowerCase();
        for (var j = 0; j < words.length; j++) {
            if (hay.indexOf(words[j]) >= 0) { out.push(rows[i]); break; }
        }
    }
    return out;
}

// full delivery flow: search ABB for a book → rows. Caller picks a row, then calls
// fetchInfoHash(slug) to resolve the torrent hash. done({ rows }) or done(null) on miss.
function resolveAudiobook(title, author, done) {
    var term = searchTerm(title, author);
    if (!term) { done(null); return; }
    requestText(ABB + "/?s=" + encodeURIComponent(term), function(html) {
        if (!html) { done(null); return; }
        var rows = relevantRows(parseSearch(html), title, author);
        if (rows.length === 0) { done(null); return; }
        done({ rows: rows });
    });
}

function fetchInfoHash(slug, done) {
    requestText(ABB + "/abss/" + slug + "/", function(html) {
        if (!html) { done(null); return; }
        done(parseDetail(html));
    });
}

function fetchDetail(slug, done) {
    var name = String(slug || "");
    try { name = decodeURIComponent(name); } catch (e) { name = ""; }
    if (!name || /^\.+$/.test(name) || /[/\\?#]/.test(name)) {
        done(null, "invalid-detail");
        return { cancel: function() {} };
    }
    return requestText(ABB + "/abss/" + encodeURIComponent(name) + "/", function(html, reason) {
        done(html ? parseDetail(html) : null, reason);
    });
}

// WordPress pager is authoritative; lack of a next-page link means exhausted.
function pageNumber(page) {
    var n = Number(page);
    return isFinite(n) && n >= 1 && n === Math.floor(n) ? n : 1;
}

function parsePage(html, page) {
    var text = String(html || "");
    var n = pageNumber(page);
    var pager = /<div class=["']wp-pagenavi["'][^>]*>([\s\S]*?)<\/div>/i.exec(text);
    var hasMore = false;
    if (pager) {
        var re = /<a\b[^>]*href=["']([^"']+)["'][^>]*>/gi, m;
        while ((m = re.exec(pager[1])) !== null) {
            var next = /\/page\/(\d+)\//.exec(m[1]);
            if (next && Number(next[1]) === n + 1) { hasMore = true; break; }
        }
    }
    return { rows: parseSearch(text), page: n, hasMore: hasMore };
}

var abbStore = null;
var ABB_CACHE_TTL_SEC = 6 * 60 * 60;

// Uses a caller-injected get(key)/put(key, value) store, not an ABB database.
function setAbbCacheStore(store) {
    abbStore = store && typeof store.get === "function" && typeof store.put === "function"
             ? store : null;
}

function abbCacheGet(key) {
    if (!abbStore) return null;
    try {
        var entry = abbStore.get("abb:" + key);
        if (!entry || typeof entry.expiresAt !== "number" || !entry.value) return null;
        return { value: entry.value, stale: Date.now() >= entry.expiresAt };
    } catch (e) { return null; }
}

function abbCachePut(key, value, ttlSec) {
    if (!abbStore) return false;
    var ttl = Number(ttlSec);
    if (!(ttl > 0) || !isFinite(ttl)) ttl = ABB_CACHE_TTL_SEC;
    try {
        abbStore.put("abb:" + key, { value: value, expiresAt: Date.now() + ttl * 1000 });
        return true;
    } catch (e) { return false; }
}

function copyPage(result, stale, offline) {
    return {
        rows: result.rows, page: result.page, hasMore: result.hasMore,
        stale: !!stale, offline: !!offline
    };
}

function browse(url, page, done) {
    var cached = abbCacheGet(url);
    if (cached && !cached.stale) {
        done(copyPage(cached.value, false, false));
        return { cancel: function() {} };
    }
    // Immediate stale display; an eventual success or failure updates it.
    if (cached) done(copyPage(cached.value, true, false));
    return requestText(url, function(html, reason) {
        if (html !== null && html !== undefined) {
            var data = parsePage(html, page);
            abbCachePut(url, data, ABB_CACHE_TTL_SEC);
            done(copyPage(data, false, false));
        } else if (cached) {
            done(copyPage(cached.value, true, reason === "offline"), reason);
        } else {
            done({ rows: [], page: page, hasMore: false, stale: false, offline: reason === "offline" }, reason);
        }
    });
}

function browseSlug(slug) {
    var s = String(slug || "").trim().toLowerCase();
    return /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(s) && s.length <= 80 ? s : "";
}

function latest(page, done) {
    var n = pageNumber(page);
    return browse(ABB + (n === 1 ? "/" : "/page/" + n + "/"), n, done);
}

function category(slug, page, done) {
    var n = pageNumber(page), s = browseSlug(slug);
    if (!s) { done({ rows: [], page: n, hasMore: false }); return { cancel: function() {} }; }
    var base = ABB + "/audio-books/type/" + s + "/";
    return browse(n === 1 ? base : base + "page/" + n + "/", n, done);
}

function language(slug, page, done) {
    var n = pageNumber(page), s = browseSlug(slug);
    if (!s) { done({ rows: [], page: n, hasMore: false }); return { cancel: function() {} }; }
    var base = ABB + "/audio-books/tag/" + s + "/";
    return browse(n === 1 ? base : base + "page/" + n + "/", n, done);
}

function search(term, page, done) {
    var n = pageNumber(page);
    var query = String(term || "").trim().toLowerCase();
    if (!query) { done({ rows: [], page: n, hasMore: false }); return { cancel: function() {} }; }
    var url = ABB + (n === 1 ? "/" : "/page/" + n + "/")
            + "?s=" + encodeURIComponent(query).replace(/%20/g, "+");
    return browse(url, n, done);
}
